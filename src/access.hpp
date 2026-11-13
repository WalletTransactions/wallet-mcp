// Доступ (спека mcp-access): каждый запрос несёт `Authorization: Bearer`
// одного из двух видов (change add-jwt-access, design D1):
// - `wmcp_…` — персональный токен, обмен в google-auth через
//   /internal/mcp/token;
// - JWT RS256 от google-auth с aud ∋ wallet-mcp и scope ∋ mcp:read — сервис
//   проверяет его локально по JWKS (D2) и только потом меняет через
//   /internal/token/exchange (RFC 8693).
// JWT для wallet-report кешируется в памяти по SHA-256 предъявленного токена
// (D3). Сам предъявленный токен в кеше не хранится; подпись JWT для
// wallet-report сервис не проверяет — это делает wallet-report.
#pragma once

#include <chrono>
#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "auth_client.hpp"
#include "clients/reports_client.hpp"
#include "nlohmann/json.hpp"
#include "util/jwks.hpp"

namespace mcpaccess {

using Clock = std::function<std::chrono::steady_clock::time_point()>;
Clock steady_clock();
// Часы для exp входящего JWT (секунды Unix).
using WallClock = std::function<std::time_t()>;
WallClock wall_clock();

// Тексты отказов — дословно из спеки mcp-access.
extern const char* const kInvalidTokenMessage;
extern const char* const kAuthUnavailableMessage;

// Адресат и scope JWT, который принимает сервис.
inline constexpr const char* kJwtAudience = "wallet-mcp";
inline constexpr const char* kJwtScope = "mcp:read";

// Вид учётных данных: лейбл метрик и поле журнала `credential`.
enum class Credential { Pat, Jwt };
const char* to_string(Credential c); // "pat" / "jwt"

// Ровно `wmcp_` + 43 символа base64url (pattern токена в openapi.yaml
// google-auth). Остальное в /internal/mcp/token не отправляется.
bool looks_like_mcp_token(const std::string& token);

// Токен из `Authorization: Bearer <токен>`; пусто — заголовка нет или он не
// Bearer.
std::string bearer_token(const std::string& authorization_header);

// SHA-256 токена, hex — ключ кешей (JWT и справочника счетов).
std::string token_key(const std::string& token);

// Публичный ключ google-auth по kid; nullopt — ключа нет или google-auth
// недоступен (jwks::Cache причин не различает, design D2).
using KeyLookup = std::function<std::optional<jwks::PublicKey>(const std::string& kid)>;

// jwks::Cache, созданный при первом обращении, а не на старте: сервис
// стартует и при недоступном google-auth (спека service-endpoints).
KeyLookup lazy_jwks(const std::string& auth_service_url);

// Цепочка actor'ов из claim act (RFC 8693 §4.1), от ближайшего к дальнему,
// через запятую: {"sub":"wallet-mcp","act":{"sub":"wallet-ai"}} →
// "wallet-mcp,wallet-ai". Нет act — пустая строка.
std::string actor_chain(const nlohmann::json& payload);

struct VerifiedJwt {
    bool ok = false;
    std::string reason;  // при отказе — для поля reason в журнале, без токена
    std::string user_id; // sub
    std::time_t exp = 0;
    std::string actor;   // actor_chain
};

// Локальная проверка (D2): kid → ключ → подпись RS256 и exp →
// iss google-auth, aud ∋ wallet-mcp, scope ∋ mcp:read, непустой sub.
// google-auth не вызывается (кроме загрузки JWKS внутри keys).
VerifiedJwt verify_jwt(const std::string& token, const KeyLookup& keys);

// Предъявленные учётные данные одного запроса.
struct Presented {
    std::string token;
    Credential kind = Credential::Pat;
    std::string user_id;       // для JWT — sub (в ответе обмена user_id нет)
    std::time_t not_after = 0; // для JWT — exp: кеш его не переживает (D3)

    static Presented pat(std::string token) { return {std::move(token), Credential::Pat, {}, 0}; }
};

struct Session {
    auth::Exchange::Result result = auth::Exchange::Result::Unavailable;
    std::string key; // token_key
    std::string jwt;
    std::string user_id;
};

class TokenCache {
public:
    // Запас до exp: JWT, которому осталось меньше, меняется заново.
    static constexpr std::chrono::seconds kExpiryMargin{30};

    // audience — адресат JWT: пусто — wallet-report; "wallet-docs" — отдельный
    // экземпляр кеша для справки (change docs-tool, D2): записи двух кешей не
    // пересекаются, метрики обмена docs — свои.
    TokenCache(auth::AuthApi& api, Clock clock = steady_clock(), WallClock wall = wall_clock(),
               std::string audience = "");

    // Попадание — JWT из кеша (mcp_token_cache_total{result="hit"}); иначе
    // обмен в google-auth (miss + mcp_token_exchange_total{result,kind}).
    // PAT — /internal/mcp/token, JWT — /internal/token/exchange.
    Session get(const Presented& presented);
    Session get(const std::string& mcp_token) { return get(Presented::pat(mcp_token)); }

    // Выбросить JWT этого токена и обменять заново (wallet-report ответил 401).
    Session refresh(const Presented& presented);
    Session refresh(const std::string& mcp_token) { return refresh(Presented::pat(mcp_token)); }

    std::size_t size() const;

private:
    struct Entry {
        std::string jwt;
        std::string user_id;
        std::chrono::steady_clock::time_point valid_until;
    };

    Session exchange(const Presented& presented, const std::string& key);
    void sweep_locked(std::chrono::steady_clock::time_point now);

    auth::AuthApi& api_;
    Clock clock_;
    WallClock wall_;
    std::string audience_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, Entry> entries_;
    unsigned calls_ = 0;
};

// Отказ по токену посреди вызова инструмента: HTTP 401 из «Отказа в доступе».
struct AuthRejected : std::runtime_error {
    AuthRejected() : std::runtime_error("mcp token rejected") {}
};
// google-auth недоступен: HTTP 503.
struct AuthUnavailable : std::runtime_error {
    AuthUnavailable() : std::runtime_error("google-auth unavailable") {}
};

// wallet-report от имени пользователя одного запроса: на 401 закешированного
// JWT — один повторный обмен и один повтор вызова на весь запрос (при смене
// ключа google-auth не будет цикла). Второй 401 — AuthRejected. Одинаково для
// обоих видов учётных данных.
class UserReports {
public:
    UserReports(reports::ReportsApi& api, TokenCache& cache, Presented presented, Session session);
    UserReports(reports::ReportsApi& api, TokenCache& cache, std::string mcp_token, Session session)
        : UserReports(api, cache, Presented::pat(std::move(mcp_token)), std::move(session)) {}

    // Ответ wallet-report как есть, кроме 401 (см. выше).
    reports::Response get(const std::string& path_and_query);

    const std::string& key() const { return session_.key; }

private:
    reports::ReportsApi& api_;
    TokenCache& cache_;
    Presented presented_;
    Session session_;
    bool retried_ = false;
};

} // namespace mcpaccess

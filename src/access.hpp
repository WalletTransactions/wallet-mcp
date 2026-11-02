// Доступ (спека mcp-access, design D3): каждый запрос несёт
// `Authorization: Bearer wmcp_…`; сервис меняет MCP-токен в google-auth на
// JWT для wallet-report и кеширует JWT в памяти по SHA-256 токена. Сам
// MCP-токен в кеше не хранится, подпись JWT сервис не проверяет — это
// делает wallet-report.
#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "auth_client.hpp"
#include "clients/reports_client.hpp"

namespace mcpaccess {

using Clock = std::function<std::chrono::steady_clock::time_point()>;
Clock steady_clock();

// Тексты отказов — дословно из спеки mcp-access.
extern const char* const kInvalidTokenMessage;
extern const char* const kAuthUnavailableMessage;

// Ровно `wmcp_` + 43 символа base64url (pattern токена в openapi.yaml
// google-auth). Остальное в google-auth не отправляется.
bool looks_like_mcp_token(const std::string& token);

// Токен из `Authorization: Bearer <токен>`; пусто — заголовка нет или он не
// Bearer.
std::string bearer_token(const std::string& authorization_header);

// SHA-256 токена, hex — ключ кешей (JWT и справочника счетов).
std::string token_key(const std::string& mcp_token);

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

    TokenCache(auth::AuthApi& api, Clock clock = steady_clock());

    // Попадание — JWT из кеша (mcp_token_cache_total{result="hit"}); иначе
    // обмен в google-auth (miss + mcp_token_exchange_total{result}).
    Session get(const std::string& mcp_token);

    // Выбросить JWT этого токена и обменять заново (wallet-report ответил 401).
    Session refresh(const std::string& mcp_token);

    std::size_t size() const;

private:
    struct Entry {
        std::string jwt;
        std::string user_id;
        std::chrono::steady_clock::time_point valid_until;
    };

    Session exchange(const std::string& mcp_token, const std::string& key);
    void sweep_locked(std::chrono::steady_clock::time_point now);

    auth::AuthApi& api_;
    Clock clock_;
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
// ключа google-auth не будет цикла). Второй 401 — AuthRejected.
class UserReports {
public:
    UserReports(reports::ReportsApi& api, TokenCache& cache, std::string mcp_token, Session session);

    // Ответ wallet-report как есть, кроме 401 (см. выше).
    reports::Response get(const std::string& path_and_query);

    const std::string& key() const { return session_.key; }

private:
    reports::ReportsApi& api_;
    TokenCache& cache_;
    std::string mcp_token_;
    Session session_;
    bool retried_ = false;
};

} // namespace mcpaccess

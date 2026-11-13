// Клиент google-auth — два обмена на короткий JWT для wallet-report:
// - персональный MCP-токен: POST {AUTH_SERVICE_URL}/internal/mcp/token
//   (operationId exchangeMcpToken, change add-mcp-personal-tokens);
// - JWT для wallet-mcp: POST {AUTH_SERVICE_URL}/internal/token/exchange
//   (RFC 8693, actor wallet-mcp, audience=wallet-report; change
//   add-token-exchange, design D4 add-jwt-access).
// Контракты — openapi.yaml google-auth. Ручки служебные: гейтвей их не
// проксирует, сервис ходит в google-auth по сети контура.
//
// Свой модуль, а не clients/reports_client из wallet-common: тот умеет
// только GET с Bearer-токеном.
#pragma once

#include <memory>
#include <string>

namespace auth {

struct Exchange {
    enum class Result { Ok, Invalid, Unavailable };
    Result result = Result::Unavailable;
    std::string access_token; // при Ok
    int expires_in = 0;       // секунд, при Ok
    std::string user_id;      // при Ok — для логов (только /internal/mcp/token)
    std::string error;        // при Unavailable — для лога, без токена
    // google-auth ответил 401 invalid_client на /internal/token/exchange:
    // неверный AUTH_EXCHANGE_SECRET — ошибка конфигурации, а не токена
    // (Result::Unavailable, в журнал — token_exchange_invalid_client).
    bool invalid_client = false;
};

const char* to_string(Exchange::Result r);

class AuthApi {
public:
    virtual ~AuthApi() = default;
    // wmcp_… → JWT для адресата audience (пусто — wallet-report, прежнее
    // поведение: поле в запрос не добавляется; change docs-tool, D2/D3).
    virtual Exchange exchange(const std::string& mcp_token, const std::string& audience = "") = 0;
    // JWT для wallet-mcp → JWT для адресата audience (RFC 8693; пусто —
    // wallet-report).
    virtual Exchange exchange_jwt(const std::string& subject_jwt, const std::string& audience = "") = 0;
};

// Разбор ответа /internal/mcp/token: 200 с access_token и expires_in > 0 —
// Ok; 401 — Invalid (google-auth причин не различает); любой другой код или
// битый 200 — Unavailable: токен при этом недействительным не считается.
Exchange parse_mcp_token_response(int status, const std::string& body);

// Разбор ответа /internal/token/exchange (design D4):
// - 200 с access_token и expires_in > 0 — Ok;
// - 400 (invalid_grant, invalid_target, invalid_scope, invalid_request и
//   любой другой код ошибки RFC 6749) — Invalid;
// - 401 invalid_client — Unavailable с invalid_client = true;
// - битый 200, 5xx и прочее — Unavailable.
Exchange parse_token_exchange_response(int status, const std::string& body);

// Сеть и таймаут — Unavailable. exchange_secret — X-Service-Secret actor'а
// wallet-mcp для /internal/token/exchange; пустой — exchange_jwt сразу
// Unavailable (путь JWT выключен, до него запрос не доходит).
std::unique_ptr<AuthApi> make_http_api(const std::string& base_url, int connect_timeout_ms = 3000,
                                       int read_timeout_ms = 5000, std::string exchange_secret = "");

} // namespace auth

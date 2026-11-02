// Клиент google-auth: обмен персонального MCP-токена на короткий JWT для
// wallet-report — POST {AUTH_SERVICE_URL}/internal/mcp/token (operationId
// exchangeMcpToken, контракт — openapi.yaml google-auth, change
// add-mcp-personal-tokens). Ручка служебная: гейтвей её не проксирует,
// сервис ходит в google-auth по сети контура.
//
// Свой модуль, а не clients/reports_client из wallet-common: тот умеет
// только GET с Bearer-токеном (design D3).
#pragma once

#include <memory>
#include <string>

namespace auth {

struct Exchange {
    enum class Result { Ok, Invalid, Unavailable };
    Result result = Result::Unavailable;
    std::string access_token; // при Ok
    int expires_in = 0;       // секунд, при Ok
    std::string user_id;      // при Ok — для логов
    std::string error;        // при Unavailable — для лога, без токена
};

const char* to_string(Exchange::Result r);

class AuthApi {
public:
    virtual ~AuthApi() = default;
    virtual Exchange exchange(const std::string& mcp_token) = 0;
};

// 200 с access_token и expires_in > 0 — Ok; 401 — Invalid (google-auth
// причин не различает); сеть, таймаут, любой другой код или битый 200 —
// Unavailable: токен при этом недействительным не считается.
std::unique_ptr<AuthApi> make_http_api(const std::string& base_url, int connect_timeout_ms = 3000,
                                       int read_timeout_ms = 5000);

} // namespace auth

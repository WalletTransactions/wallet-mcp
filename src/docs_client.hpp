// Клиент wallet-docs: POST {WALLET_DOCS_URL}/api/docs/ask с JWT для
// wallet-docs (change docs-tool, D1, D8). Ходит напрямую по сети контура, не
// через гейтвей — как wallet-report. Контракт — openapi.yaml wallet-docs.
#pragma once

#include <memory>
#include <string>

#include "nlohmann/json.hpp"

namespace docsclient {

struct Response {
    int status = 0; // 0 — до HTTP-ответа не дошло (сеть, таймаут)
    nlohmann::json body; // null, если тело не JSON
    int retry_after = 0; // секунд, заголовок Retry-After (429); 0 — нет
    std::string error;   // для лога, без текста вопроса
};

class DocsApi {
public:
    virtual ~DocsApi() = default;
    virtual Response ask(const std::string& question, const std::string& access_token) = 0;
};

// Соединение 3 с, чтение 25 с (D8: короче таймаута гейтвея в 30 с).
std::unique_ptr<DocsApi> make_http_api(const std::string& base_url, int connect_timeout_ms = 3000,
                                       int read_timeout_ms = 25000);

} // namespace docsclient

// HTTP-эндпоинт MCP: POST /api/mcp/ (JSON-RPC), GET/DELETE /api/mcp/ — 405.
// Порядок проверок (спеки mcp-transport, mcp-access): лимит тела 64 КБ
// (413, httplib до хендлера) → Authorization: Bearer (401) → для JWT —
// локальная проверка по JWKS (401) → обмен в google-auth (401/503) →
// MCP-Protocol-Version → разбор JSON-RPC.
#pragma once

#include <ctime>
#include <functional>
#include <string>

#include "access.hpp"
#include "clients/reports_client.hpp"
#include "directory.hpp"
#include "docs_client.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"

namespace mcphttp {

constexpr std::size_t kMaxBodyBytes = 64 * 1024;
constexpr const char* kPath = "/api/mcp/";

struct Deps {
    mcpaccess::TokenCache& tokens;
    reports::ReportsApi& reports;
    directory::Cache& directory;
    std::string tz_name;
    std::string server_version;
    std::function<std::time_t()> now = [] { return std::time(nullptr); };
    // Ключи google-auth для JWT (mcpaccess::lazy_jwks). Пусто — приём JWT
    // выключен (AUTH_EXCHANGE_SECRET не задан): токен не `wmcp_…` — 401.
    mcpaccess::KeyLookup jwt_keys;
    // Справка (change docs-tool): оба пустые — WALLET_DOCS_URL не задан,
    // инструмента ask_docs нет. docs_tokens — кеш с audience wallet-docs.
    docsclient::DocsApi* docs = nullptr;
    mcpaccess::TokenCache* docs_tokens = nullptr;
};

// Поля журнала о текущем запросе к /api/mcp/ после аутентификации:
// credential (pat/jwt) и для JWT actor (цепочка act). Пустой объект, если
// запрос не дошёл до аутентификации. thread_local, как контекст applog:
// main.cpp добавляет поля в request_finished и сразу очищает.
nlohmann::json request_log_fields();
void clear_request_log_fields();

void handle_post(const httplib::Request& req, httplib::Response& res, Deps& deps);

void register_routes(httplib::Server& svr, Deps& deps);

} // namespace mcphttp

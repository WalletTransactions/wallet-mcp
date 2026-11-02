// HTTP-эндпоинт MCP: POST /api/mcp/ (JSON-RPC), GET/DELETE /api/mcp/ — 405.
// Порядок проверок (спеки mcp-transport, mcp-access): лимит тела 64 КБ
// (413, httplib до хендлера) → Authorization: Bearer wmcp_… (401) → обмен в
// google-auth (401/503) → MCP-Protocol-Version → разбор JSON-RPC.
#pragma once

#include <ctime>
#include <functional>
#include <string>

#include "access.hpp"
#include "clients/reports_client.hpp"
#include "directory.hpp"
#include "httplib.h"

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
};

void handle_post(const httplib::Request& req, httplib::Response& res, Deps& deps);

void register_routes(httplib::Server& svr, Deps& deps);

} // namespace mcphttp

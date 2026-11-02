#include "mcp_http.hpp"

#include "dates.hpp"
#include "mcp.hpp"
#include "nlohmann/json.hpp"
#include "tools.hpp"
#include "util/log.hpp"
#include "util/uuid.hpp"

using nlohmann::json;

namespace mcphttp {

namespace {

constexpr const char* kJson = "application/json";

// Отказ по токену. В лог — только причина: ни токена, ни его хэша.
void reject(httplib::Response& res, const char* reason) {
    applog::warn("MCP-запрос отклонён: токен недействителен", {{"event", "mcp_auth_rejected"}, {"reason", reason}});
    res.status = 401;
    res.set_header("WWW-Authenticate", "Bearer");
    res.set_content(json{{"error", "invalid_token"}, {"message", mcpaccess::kInvalidTokenMessage}}.dump(), kJson);
}

void auth_unavailable(httplib::Response& res) {
    res.status = 503;
    res.set_content(json{{"error", "unavailable"}, {"message", mcpaccess::kAuthUnavailableMessage}}.dump(), kJson);
}

void method_not_allowed(const httplib::Request& req, httplib::Response& res) {
    applog::set_route(req.method + " /api/mcp/");
    // Серверных уведомлений нет — потока (SSE) сервис не открывает, сессий не
    // хранит (D1, D2).
    res.status = 405;
    res.set_header("Allow", "POST");
    res.set_content(json{{"error", "method_not_allowed"}, {"message", "MCP: только POST /api/mcp/"}}.dump(), kJson);
}

} // namespace

void handle_post(const httplib::Request& req, httplib::Response& res, Deps& deps) {
    applog::set_route("POST /api/mcp/");

    std::string token = mcpaccess::bearer_token(req.get_header_value("Authorization"));
    if (token.empty()) return reject(res, "missing");
    if (!mcpaccess::looks_like_mcp_token(token)) return reject(res, "format");

    mcpaccess::Session session = deps.tokens.get(token);
    if (session.result == auth::Exchange::Result::Invalid) return reject(res, "google_auth");
    if (session.result == auth::Exchange::Result::Unavailable) return auth_unavailable(res);
    if (!session.user_id.empty()) applog::set_user(session.user_id);

    // Клиент ревизии 2025-06-18+ шлёт версию после рукопожатия; незнакомую —
    // 400 (так велит 2025-06-18). Клиент новых ревизий без initialize
    // (2026-07-28) по такому 400 откатывается на рукопожатие.
    std::string protocol = req.get_header_value("MCP-Protocol-Version");
    if (!mcp::protocol_header_ok(protocol)) {
        res.status = 400;
        std::string supported;
        for (const auto& v : mcp::known_protocol_versions()) supported += (supported.empty() ? "" : ", ") + v;
        res.set_content(json{{"jsonrpc", "2.0"},
                             {"id", nullptr},
                             {"error", {{"code", -32000},
                                        {"message", "Bad Request: unsupported MCP-Protocol-Version " + protocol +
                                                        " (supported: " + supported + ")"}}}}
                            .dump(),
                        kJson);
        return;
    }

    mcpaccess::UserReports reports(deps.reports, deps.tokens, token, session);
    tools::Context ctx{reports, deps.directory, dates::today(deps.now())};
    mcp::Handlers h;
    h.today = ctx.today;
    h.tz_name = deps.tz_name;
    h.server_version = deps.server_version;
    h.new_session_id = [] { return uuidgen::v4(); };
    h.call_tool = [&ctx](const std::string& name, const json& arguments) { return tools::call(name, arguments, ctx); };

    mcp::Reply reply;
    try {
        reply = mcp::handle(req.body, h);
    } catch (const mcpaccess::AuthRejected&) {
        return reject(res, "wallet_report");
    } catch (const mcpaccess::AuthUnavailable&) {
        return auth_unavailable(res);
    }
    res.status = reply.status;
    for (const auto& [k, v] : reply.headers) res.set_header(k, v);
    if (!reply.body.empty()) res.set_content(reply.body, kJson);
}

void register_routes(httplib::Server& svr, Deps& deps) {
    svr.Post(kPath, [&deps](const httplib::Request& req, httplib::Response& res) { handle_post(req, res, deps); });
    svr.Get(kPath, method_not_allowed);
    svr.Delete(kPath, method_not_allowed);
}

} // namespace mcphttp

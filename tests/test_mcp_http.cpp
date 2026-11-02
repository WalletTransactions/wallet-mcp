// HTTP-слой MCP: аутентификация до разбора JSON-RPC, тексты отказов, лимит
// тела, 405 и то, что попадает в лог.
#include <thread>

#include "fakes.hpp"
#include "httplib.h"
#include "mcp_http.hpp"
#include "nlohmann/json.hpp"
#include "test_framework.hpp"
#include "util/log.hpp"

using nlohmann::json;

namespace {

struct Env {
    fakes::FakeAuth auth;
    fakes::FakeReports reports;
    mcpaccess::TokenCache tokens{auth};
    directory::Cache directory;
    mcphttp::Deps deps{tokens, reports, directory, "UTC", "0.1.0", [] { return std::time_t{1793448000}; }};

    Env() { reports.set("/api/reports/accounts", fakes::accounts_body()); }
    // Контекст лога (user_id, route) — thread_local, как в сервере после set_logger.
    ~Env() { applog::clear_request_context(); }

    httplib::Response post(const std::string& body, const std::string& authorization,
                           const httplib::Headers& extra = {}) {
        httplib::Request req;
        req.method = "POST";
        req.path = "/api/mcp/";
        req.body = body;
        if (!authorization.empty()) req.headers.emplace("Authorization", authorization);
        for (const auto& h : extra) req.headers.insert(h);
        httplib::Response res;
        mcphttp::handle_post(req, res, deps);
        return res;
    }
};

const std::string kInit =
    R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})";
const std::string kMessage =
    "MCP-токен недействителен или истёк — выпустите новый в веб-кабинете: Настройки → Устройства";

void check_invalid_token(const httplib::Response& res) {
    CHECK_EQ(res.status, 401);
    CHECK_EQ(res.get_header_value("WWW-Authenticate"), "Bearer");
    CHECK(res.get_header_value("Content-Type").find("application/json") == 0);
    CHECK_EQ(json::parse(res.body), (json{{"error", "invalid_token"}, {"message", kMessage}}));
}

} // namespace

TEST(http_no_authorization_401) {
    Env e;
    fakes::CaptureLog log;
    check_invalid_token(e.post(kInit, ""));
    CHECK_EQ(e.auth.calls, 0);
    CHECK(e.reports.requests.empty());
    CHECK(log.text().find("mcp_auth_rejected") != std::string::npos);
}

TEST(http_jwt_instead_of_mcp_token_401_without_exchange) {
    Env e;
    check_invalid_token(e.post(kInit, "Bearer eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiJ1In0.c2ln"));
    CHECK_EQ(e.auth.calls, 0);
}

TEST(http_revoked_token_401_and_log_has_no_token) {
    Env e;
    e.auth.invalid();
    fakes::CaptureLog log;
    check_invalid_token(e.post(kInit, "Bearer " + fakes::token()));
    CHECK_EQ(e.auth.calls, 1);
    std::string text = log.text();
    CHECK(text.find("mcp_auth_rejected") != std::string::npos);
    CHECK(text.find("wmcp_") == std::string::npos);
    CHECK(text.find(mcpaccess::token_key(fakes::token())) == std::string::npos);
}

TEST(http_google_auth_down_503) {
    Env e;
    e.auth.unavailable();
    httplib::Response res = e.post(kInit, "Bearer " + fakes::token());
    CHECK_EQ(res.status, 503);
    CHECK_EQ(json::parse(res.body),
             (json{{"error", "unavailable"}, {"message", "Сервис авторизации временно недоступен, попробуйте позже"}}));
    // google-auth поднялся — следующий запрос проходит.
    e.auth.ok();
    CHECK_EQ(e.post(kInit, "Bearer " + fakes::token()).status, 200);
}

TEST(http_initialize_and_notifications_need_token_too) {
    Env e;
    check_invalid_token(e.post(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", ""));
    httplib::Response res = e.post(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", "Bearer " + fakes::token());
    CHECK_EQ(res.status, 202);
    CHECK(res.body.empty());
}

TEST(http_initialize_ok_with_session_header) {
    Env e;
    httplib::Response res = e.post(kInit, "Bearer " + fakes::token());
    CHECK_EQ(res.status, 200);
    CHECK_EQ(res.get_header_value("Mcp-Session-Id").size(), 36u);
    json r = json::parse(res.body)["result"];
    CHECK_EQ(r["serverInfo"]["name"], "wallet-mcp");
    // 1793448000 — 2026-10-31 12:00 UTC.
    CHECK(r["instructions"].get<std::string>().find("2026-10-31") != std::string::npos);
}

TEST(http_session_id_does_not_identify) {
    Env e;
    // Чужой или старый Mcp-Session-Id — данные по токену запроса, без повторного initialize.
    httplib::Response res = e.post(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_categories"}})",
                                   "Bearer " + fakes::token(), {{"Mcp-Session-Id", "00000000-0000-4000-8000-000000000000"}});
    CHECK_EQ(res.status, 200);
    CHECK_EQ(json::parse(res.body)["result"]["isError"], false);
    REQUIRE(!e.auth.tokens.empty());
    CHECK_EQ(e.auth.tokens.back(), fakes::token());
}

TEST(http_unknown_protocol_header_400) {
    Env e;
    httplib::Response res = e.post(R"({"jsonrpc":"2.0","id":1,"method":"server/discover"})", "Bearer " + fakes::token(),
                                   {{"MCP-Protocol-Version", "2026-07-28"}});
    CHECK_EQ(res.status, 400);
    res = e.post(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})", "Bearer " + fakes::token(),
                 {{"MCP-Protocol-Version", "2025-06-18"}});
    CHECK_EQ(res.status, 200);
}

TEST(http_report_401_twice_is_401) {
    Env e;
    e.reports.set("/api/reports/accounts", {{"error", "unauthorized"}}, 401);
    httplib::Response res = e.post(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_categories"}})",
                                   "Bearer " + fakes::token());
    check_invalid_token(res);
    CHECK_EQ(e.auth.calls, 2);
}

TEST(http_tool_log_has_no_arguments_or_sums) {
    Env e;
    e.reports.set("/api/reports/operations",
                  {{"operations",
                    {{{"operation_uuid", "o1"}, {"date", "2026-09-28T12:05:00"}, {"kind", "expense"},
                      {"amount", "777.77"}, {"currency_uuid", "c-RUB"}, {"symbol", "₽"}, {"precision", 2},
                      {"amount_eq", "777.77"}, {"comment", "няня Маша, аванс"}, {"category_uuid", "c-nanny"},
                      {"category_name", "Няня"}, {"icon_sha256", "x"}, {"wallet_uuid", "w-cash"},
                      {"wallet_name", "Наличные"}, {"from_uuid", "w-cash"}, {"from_name", "Наличные"},
                      {"to_uuid", "c-nanny"}, {"to_name", "Няня"}}}},
                   {"incomplete", false}, {"has_more", false}, {"eq_currency_uuid", "c-RUB"},
                   {"total_eq", "777.77"}, {"total_incomplete", false}, {"total_count", 1}});
    fakes::CaptureLog log;
    httplib::Response res = e.post(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"search_operations","arguments":{"from":"2026-09-01","to":"2026-09-30","text":"няня Маша"}}})",
        "Bearer " + fakes::token());
    CHECK_EQ(res.status, 200);
    CHECK(res.body.find("777.77") != std::string::npos);
    std::string text = log.text();
    CHECK(text.find("\"tool\":\"search_operations\"") != std::string::npos);
    CHECK(text.find("\"result\":\"ok\"") != std::string::npos);
    CHECK(text.find("\"user_id\"") != std::string::npos);
    CHECK(text.find("Маша") == std::string::npos);
    CHECK(text.find("777") == std::string::npos);
    CHECK(text.find("Наличные") == std::string::npos);
    CHECK(text.find("wmcp_") == std::string::npos);
    CHECK(text.find("jwt-") == std::string::npos);

    // Отказ wallet-report — тоже без query (в ней даты и текст поиска).
    e.reports.set("/api/reports/operations", {{"error", "boom"}}, 500);
    e.post(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"search_operations","arguments":{"from":"2026-09-01","to":"2026-09-30","text":"няня Маша"}}})",
           "Bearer " + fakes::token());
    text = log.text();
    CHECK(text.find("\"result\":\"unavailable\"") != std::string::npos);
    CHECK(text.find("Маша") == std::string::npos);
    CHECK(text.find("2026-09-01") == std::string::npos);
    applog::clear_request_context();
}

TEST(http_server_413_before_auth_and_405_on_get) {
    Env e;
    httplib::Server svr;
    svr.set_payload_max_length(mcphttp::kMaxBodyBytes);
    mcphttp::register_routes(svr, e.deps);
    int port = svr.bind_to_any_port("127.0.0.1");
    std::thread th([&svr] { svr.listen_after_bind(); });
    svr.wait_until_ready();

    httplib::Client cli("127.0.0.1", port);
    std::string huge(1024 * 1024, ' ');
    auto res = cli.Post("/api/mcp/", {{"Authorization", "Bearer " + fakes::token()}}, huge, "application/json");
    REQUIRE(res);
    CHECK_EQ(res->status, 413);
    CHECK_EQ(e.auth.calls, 0);
    CHECK(e.reports.requests.empty());

    // Ровно на границе лимита — уже аутентификация (без токена — 401).
    auto edge = cli.Post("/api/mcp/", std::string(mcphttp::kMaxBodyBytes - 1, ' '), "application/json");
    REQUIRE(edge);
    CHECK_EQ(edge->status, 401);

    auto get = cli.Get("/api/mcp/", httplib::Headers{{"Accept", "text/event-stream"}});
    REQUIRE(get);
    CHECK_EQ(get->status, 405);
    CHECK(get->get_header_value("Content-Type").find("text/event-stream") == std::string::npos);
    auto del = cli.Delete("/api/mcp/");
    REQUIRE(del);
    CHECK_EQ(del->status, 405);
    // Без завершающего «/» — не обслуживается.
    auto bare = cli.Post("/api/mcp", "{}", "application/json");
    REQUIRE(bare);
    CHECK_EQ(bare->status, 404);

    svr.stop();
    th.join();
}

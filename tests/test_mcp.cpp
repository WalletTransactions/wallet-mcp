// Диспетчер JSON-RPC без HTTP и без wallet-report.
#include "mcp.hpp"
#include "nlohmann/json.hpp"
#include "test_framework.hpp"

using nlohmann::json;

namespace {

int tool_calls = 0;

mcp::Handlers handlers() {
    mcp::Handlers h;
    h.today = *dates::parse("2026-10-31");
    h.tz_name = "UTC";
    h.server_version = "0.1.0";
    h.new_session_id = [] { return std::string("11111111-2222-4333-8444-555555555555"); };
    h.call_tool = [](const std::string& name, const json& args) {
        ++tool_calls;
        tools::validate(name, args);
        return tools::Outcome{tools::Outcome::Kind::Ok, R"({"categories":[]})"};
    };
    return h;
}

json call(const std::string& body, int expected_status = 200) {
    mcp::Reply r = mcp::handle(body, handlers());
    CHECK_EQ(r.status, expected_status);
    if (r.body.empty()) return nullptr;
    return json::parse(r.body);
}

int error_code(const json& j) { return j.contains("error") ? j["error"]["code"].get<int>() : 0; }

} // namespace

TEST(mcp_parse_error) {
    json j = call("{not json");
    CHECK_EQ(error_code(j), -32700);
    CHECK(j["id"].is_null());
}

TEST(mcp_invalid_request) {
    CHECK_EQ(error_code(call(R"([{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","id":2,"method":"ping"}])")),
             -32600);
    CHECK_EQ(error_code(call(R"("ping")")), -32600);
    CHECK_EQ(error_code(call(R"({"id":1,"method":"ping"})")), -32600);
    CHECK_EQ(error_code(call(R"({"jsonrpc":"1.0","id":1,"method":"ping"})")), -32600);
    CHECK_EQ(error_code(call(R"({"jsonrpc":"2.0","id":1,"method":5})")), -32600);
    CHECK_EQ(error_code(call(R"({"jsonrpc":"2.0","id":{"a":1},"method":"ping"})")), -32600);
    CHECK_EQ(error_code(call(R"({"jsonrpc":"2.0","id":1})")), -32600);
}

TEST(mcp_method_not_found) {
    json j = call(R"({"jsonrpc":"2.0","id":7,"method":"resources/list"})");
    CHECK_EQ(error_code(j), -32601);
    CHECK_EQ(j["id"], 7);
}

TEST(mcp_ping) {
    json j = call(R"({"jsonrpc":"2.0","id":"p","method":"ping"})");
    CHECK_EQ(j["id"], "p");
    CHECK(j["result"].is_object() && j["result"].empty());
}

TEST(mcp_notifications_accepted_without_body) {
    mcp::Reply r = mcp::handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", handlers());
    CHECK_EQ(r.status, 202);
    CHECK(r.body.empty());
    r = mcp::handle(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":3}})", handlers());
    CHECK_EQ(r.status, 202);
    // Ответ клиента — тоже 202.
    r = mcp::handle(R"({"jsonrpc":"2.0","id":5,"result":{}})", handlers());
    CHECK_EQ(r.status, 202);
}

TEST(mcp_initialize) {
    mcp::Reply r = mcp::handle(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"lm","version":"1"}}})",
        handlers());
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.headers["Mcp-Session-Id"], "11111111-2222-4333-8444-555555555555");
    json res = json::parse(r.body)["result"];
    CHECK_EQ(res["protocolVersion"], "2025-06-18");
    CHECK_EQ(res["capabilities"], (json{{"tools", json::object()}}));
    CHECK_EQ(res["serverInfo"]["name"], "wallet-mcp");
    CHECK_EQ(res["serverInfo"]["version"], "0.1.0");
    CHECK(res["instructions"].is_string());

    json older = call(R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2025-03-26"}})");
    CHECK_EQ(older["result"]["protocolVersion"], "2025-03-26");
    json unknown = call(R"({"jsonrpc":"2.0","id":3,"method":"initialize","params":{"protocolVersion":"2024-11-05"}})");
    CHECK_EQ(unknown["result"]["protocolVersion"], mcp::known_protocol_versions().front());
    CHECK_EQ(mcp::known_protocol_versions().front(), "2025-11-25");
}

TEST(mcp_protocol_header) {
    CHECK(mcp::protocol_header_ok(""));
    CHECK(mcp::protocol_header_ok("2025-06-18"));
    CHECK(mcp::protocol_header_ok("2025-03-26"));
    CHECK(mcp::protocol_header_ok("2025-11-25"));
    CHECK(!mcp::protocol_header_ok("2026-07-28"));
    CHECK(!mcp::protocol_header_ok("garbage"));
}

TEST(mcp_instructions_text) {
    std::string text = mcp::instructions(*dates::parse("2026-10-31"), "UTC");
    CHECK(text.find("2026-10-31") != std::string::npos);
    CHECK(text.find("суббота") != std::string::npos);
    CHECK(text.find("пояс UTC") != std::string::npos);
    CHECK(text.find("Не считай суммы сам — используй результаты инструментов.") != std::string::npos);
    CHECK(text.find("ГГГГ-ММ-ДД") != std::string::npos);
    CHECK(text.find("переспроси") != std::string::npos);
    // Готовые периоды от сегодняшней даты, без примера с чужим месяцем.
    CHECK(text.find("«этот месяц» (октябрь) — from 2026-10-01, to 2026-10-31") != std::string::npos);
    CHECK(text.find("«прошлый месяц» (сентябрь) — from 2026-09-01, to 2026-09-30") != std::string::npos);
    CHECK(text.find("без своих пояснений в скобках") != std::string::npos);
    CHECK(text.find("Среднее в месяц бери из avg_per_month, сравнение месяцев года (лето, зима) — через "
                    "months_of_year в spending_by_category_by_month; сам не дели и не складывай.") != std::string::npos);

    std::string nov = mcp::instructions(*dates::parse("2026-11-04"), "UTC");
    CHECK(nov.find("«прошлый месяц» (октябрь) — from 2026-10-01, to 2026-10-31") != std::string::npos);
    CHECK(nov.find("2026-09-01") == std::string::npos);
}

TEST(mcp_tools_list_has_exactly_seven_read_tools) {
    json j = call(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    const json& list = j["result"]["tools"];
    REQUIRE(list.is_array());
    CHECK_EQ(list.size(), 7u);
    std::vector<std::string> names;
    for (const auto& t : list) {
        names.push_back(t["name"]);
        CHECK(t["description"].is_string() && !t["description"].get<std::string>().empty());
        CHECK_EQ(t["inputSchema"]["type"], "object");
        CHECK_EQ(t["inputSchema"]["additionalProperties"], false);
        for (const char* forbidden : {"user_id", "ledger_id", "as_user_id", "token"}) {
            CHECK(!t["inputSchema"]["properties"].contains(forbidden));
        }
    }
    CHECK((names == std::vector<std::string>{"list_wallets", "list_categories", "spending_by_category",
                                             "spending_by_category_by_month", "income_expense_by_month", "balance",
                                             "search_operations"}));
}

TEST(mcp_tools_call_unknown_tool) {
    tool_calls = 0;
    json j = call(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"delete_operation","arguments":{}}})");
    CHECK_EQ(error_code(j), -32602);
    CHECK_EQ(tool_calls, 0);
    CHECK_EQ(error_code(call(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{}})")), -32602);
}

TEST(mcp_tools_call_rejects_foreign_access_args) {
    for (const char* key : {"as_user_id", "user_id", "ledger_id"}) {
        json body = {{"jsonrpc", "2.0"},
                     {"id", 9},
                     {"method", "tools/call"},
                     {"params", {{"name", "list_categories"}, {"arguments", {{key, "u-2"}}}}}};
        json j = call(body.dump());
        CHECK_EQ(error_code(j), -32602);
    }
}

TEST(mcp_tools_call_result_shape) {
    json j = call(R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"list_categories"}})");
    const json& res = j["result"];
    CHECK_EQ(res["isError"], false);
    REQUIRE(res["content"].is_array() && res["content"].size() == 1u);
    CHECK_EQ(res["content"][0]["type"], "text");
    CHECK_EQ(res["content"][0]["text"], R"({"categories":[]})");
}

TEST(mcp_schema_validation) {
    auto invalid = [](const std::string& tool, const json& args) {
        try {
            tools::validate(tool, args);
        } catch (const tools::InvalidParams&) {
            return true;
        }
        return false;
    };
    CHECK(!invalid("spending_by_category", {{"from", "2026-09-01"}, {"to", "2026-09-30"}}));
    CHECK(invalid("spending_by_category", {{"from", "2026-09-01"}}));                         // нет to
    CHECK(invalid("spending_by_category", {{"from", 20260901}, {"to", "2026-09-30"}}));       // не строка
    CHECK(invalid("spending_by_category", {{"from", "2026-09-01"}, {"to", "x"}, {"categories", "Няня"}}));
    CHECK(invalid("spending_by_category", {{"from", "a"}, {"to", "b"}, {"categories", json::array()}}));
    CHECK(invalid("spending_by_category", {{"from", "a"}, {"to", "b"}, {"categories", {1, 2}}}));
    CHECK(!invalid("spending_by_category", {{"from", "a"}, {"to", "b"}, {"categories", nullptr}}));
    CHECK(invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"kind", "spending"}}));
    CHECK(!invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"kind", "any"}}));
    CHECK(invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"limit", 0}}));
    CHECK(invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"limit", 51}}));
    CHECK(invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"limit", "20"}}));
    CHECK(invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"limit", 2.5}}));
    CHECK(!invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"limit", 50}}));
    CHECK(!invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"limit", 20.0}}));
    CHECK(invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"text", std::string(101, 'x')}}));
    // 100 символов кириллицы — 200 байт, но в лимите.
    std::string cyr;
    for (int i = 0; i < 100; ++i) cyr += "я";
    CHECK(!invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"text", cyr}}));
    // months_of_year: целые 1..12 без повторов.
    const json p = {{"from", "a"}, {"to", "b"}};
    auto moy = [&](json v) {
        json a = p;
        a["months_of_year"] = std::move(v);
        return a;
    };
    CHECK(!invalid("spending_by_category_by_month", moy({6, 7, 8})));
    CHECK(!invalid("spending_by_category_by_month", moy({12, 1, 2})));
    CHECK(!invalid("spending_by_category_by_month", moy({6.0})));
    CHECK(invalid("spending_by_category_by_month", moy({13})));
    CHECK(invalid("spending_by_category_by_month", moy({0})));
    CHECK(invalid("spending_by_category_by_month", moy({6, 6})));
    CHECK(invalid("spending_by_category_by_month", moy({"6"})));
    CHECK(invalid("spending_by_category_by_month", moy({6.5})));
    CHECK(invalid("spending_by_category_by_month", moy(json::array())));
    CHECK(invalid("spending_by_category_by_month", moy(6)));
    CHECK(invalid("spending_by_category_by_month", {{"from", "a"}, {"to", "b"}, {"compare_from", "a"}}));
    CHECK(!invalid("spending_by_category", {{"from", "a"}, {"to", "b"}, {"exclude_categories", {"Ремонт"}}}));
    CHECK(!invalid("search_operations", {{"from", "a"}, {"to", "b"}, {"exclude_categories", {"Ремонт"}}}));
    CHECK(invalid("spending_by_category", {{"from", "a"}, {"to", "b"}, {"exclude_categories", json::array()}}));
    CHECK(invalid("list_wallets", json::array()));
    CHECK(invalid("balance", {{"wallet", "x"}, {"token", "wmcp_x"}}));
}

TEST(mcp_months_of_year_out_of_range_is_invalid_params) {
    for (const char* moy : {"[13]", "[6,6]"}) {
        std::string body = std::string(R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":)"
                                       R"("spending_by_category_by_month","arguments":{"from":"2026-01-01",)"
                                       R"("to":"2026-12-31","months_of_year":)") +
                           moy + "}}}";
        CHECK_EQ(error_code(call(body)), -32602);
    }
}

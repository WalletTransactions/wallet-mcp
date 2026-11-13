// Инструмент ask_docs (change docs-tool, спека docs-lookup): фейковые
// google-auth и wallet-docs, без сети.
#include <deque>

#include "fakes.hpp"
#include "mcp.hpp"
#include "nlohmann/json.hpp"
#include "test_framework.hpp"
#include "tools.hpp"

using nlohmann::json;

namespace {

struct FakeDocs : docsclient::DocsApi {
    std::deque<docsclient::Response> queue; // последний повторяется
    std::vector<std::string> questions;
    std::vector<std::string> jwts;

    void set(int status, json body = nullptr, int retry_after = 0) {
        docsclient::Response r;
        r.status = status;
        r.body = std::move(body);
        r.retry_after = retry_after;
        queue = {r};
    }
    void push(int status, json body = nullptr) {
        docsclient::Response r;
        r.status = status;
        r.body = std::move(body);
        queue.push_back(r);
    }
    docsclient::Response ask(const std::string& q, const std::string& jwt) override {
        questions.push_back(q);
        jwts.push_back(jwt);
        docsclient::Response r = queue.front();
        if (queue.size() > 1) queue.pop_front();
        return r;
    }
};

json answer_body(int sources = 2) {
    json s = json::array();
    for (int i = 0; i < sources; ++i) s.push_back({{"repo", "wallet-web"}, {"capability", "x"}, {"heading", "h"}});
    return {{"answer", "Баланс — сумма операций."}, {"sources", s}};
}

struct DocsEnv {
    fakes::FakeAuth auth;
    fakes::FakeReports reports;
    fakes::ManualClock clock;
    mcpaccess::TokenCache tokens{auth};
    mcpaccess::TokenCache docs_tokens{auth, clock.fn(), mcpaccess::wall_clock(), "wallet-docs"};
    directory::Cache directory{clock.fn()};
    FakeDocs docs;

    tools::Outcome ask_args(const json& args, bool enabled = true) {
        mcpaccess::UserReports ur(reports, tokens, fakes::token(), tokens.get(fakes::token()));
        tools::DocsContext dc{docs, docs_tokens, mcpaccess::Presented::pat(fakes::token())};
        tools::Context ctx{ur, directory, *dates::parse("2026-10-31"), enabled ? &dc : nullptr};
        return tools::call("ask_docs", args, ctx);
    }
    tools::Outcome ask(const std::string& q, bool enabled = true) { return ask_args(json{{"question", q}}, enabled); }
};

} // namespace

TEST(docs_tools_list_states) {
    CHECK_EQ(tools::list(false)["tools"].size(), 7u);
    json with = tools::list(true);
    CHECK_EQ(with["tools"].size(), 8u);
    CHECK_EQ(with["tools"][7]["name"], "ask_docs");
    CHECK(!tools::exists("ask_docs", false));
    CHECK(tools::exists("ask_docs", true));
    const json& schema = with["tools"][7]["inputSchema"];
    CHECK_EQ(schema["additionalProperties"], false);
    CHECK_EQ(schema["properties"]["question"]["maxLength"], 1000);
    std::string d = with["tools"][7]["description"];
    CHECK(d.find("не отдаёт") != std::string::npos);
}

TEST(docs_unknown_when_disabled) {
    DocsEnv e;
    CHECK_THROWS(e.ask("как это?", false));
    CHECK(e.docs.questions.empty());
}

TEST(docs_schema_violations) {
    DocsEnv e;
    e.docs.set(200, answer_body());
    CHECK_THROWS(e.ask_args(json::object()));
    CHECK_THROWS(e.ask_args(json{{"question", 5}}));
    CHECK_THROWS(e.ask_args(json{{"question", std::string(1001, 'a')}}));
    CHECK_THROWS(e.ask_args(json{{"question", "x"}, {"role", "admin"}}));
    CHECK(e.docs.questions.empty());
}

TEST(docs_empty_question) {
    DocsEnv e;
    tools::Outcome o = e.ask("   ");
    CHECK(o.kind == tools::Outcome::Kind::ToolError);
    CHECK_EQ(o.text, "Вопрос пустой.");
    CHECK(e.docs.questions.empty());
    CHECK_EQ(e.auth.calls, 1); // только обмен для wallet-report в самом тесте
}

TEST(docs_ok_passthrough) {
    DocsEnv e;
    e.docs.set(200, answer_body(3));
    tools::Outcome o = e.ask("Как строится баланс?");
    CHECK(o.kind == tools::Outcome::Kind::Ok);
    CHECK_EQ(o.sources, 3);
    json j = json::parse(o.text);
    CHECK_EQ(j["answer"], "Баланс — сумма операций.");
    CHECK_EQ(j["sources"].size(), 3u);
    CHECK_EQ(e.docs.questions[0], "Как строится баланс?");
}

TEST(docs_no_answer_is_ok) {
    DocsEnv e;
    e.docs.set(200, json{{"answer", "В описании системы этого нет."}, {"sources", json::array()}});
    tools::Outcome o = e.ask("Как гейтвей проверяет JWT?");
    CHECK(o.kind == tools::Outcome::Kind::Ok);
    CHECK_EQ(o.sources, 0);
}

TEST(docs_jwt_exchange_audience_and_cache) {
    DocsEnv e;
    e.docs.set(200, answer_body());
    e.ask("один");
    e.ask("два");
    e.ask("три");
    // Обмен для wallet-report (audience "") + один обмен для wallet-docs на три вопроса.
    int docs_exchanges = 0;
    for (const auto& a : e.auth.audiences) docs_exchanges += a == "wallet-docs";
    CHECK_EQ(docs_exchanges, 1);
    CHECK_EQ(e.docs.jwts[0], e.docs.jwts[2]);
    // JWT для справки не совпадает с JWT для wallet-report.
    CHECK(e.docs.jwts[0] != e.tokens.get(fakes::token()).jwt);
}

TEST(docs_error_texts) {
    DocsEnv e;
    e.docs.set(429, json{{"error", "too_many_requests"}}, 54);
    tools::Outcome o = e.ask("q");
    CHECK(o.kind != tools::Outcome::Kind::Ok);
    CHECK_EQ(o.text, "Слишком много вопросов, повторите через 54 с.");

    e.docs.set(503, json{{"error", "llm_unavailable"}});
    CHECK_EQ(e.ask("q").text, "Справка временно недоступна, попробуйте позже.");

    e.docs.set(0); // сеть, таймаут
    CHECK_EQ(e.ask("q").text, "Справка временно недоступна, попробуйте позже.");

    e.docs.set(200, json{{"oops", 1}}); // битый ответ
    CHECK_EQ(e.ask("q").text, "Справка временно недоступна, попробуйте позже.");

    e.docs.set(403, json{{"error", "forbidden"}});
    CHECK_EQ(e.ask("q").text, "Справка недоступна для этого токена.");

    e.docs.set(400, json{{"error", "question too long"}});
    CHECK_EQ(e.ask("q").text, "Вопрос не принят: question too long.");
}

TEST(docs_retry_once_on_401) {
    DocsEnv e;
    e.docs.push(401, json{{"error", "unauthorized"}});
    e.docs.push(200, answer_body());
    tools::Outcome o = e.ask("q");
    CHECK(o.kind == tools::Outcome::Kind::Ok);
    CHECK_EQ(e.docs.questions.size(), 2u);
    CHECK(e.docs.jwts[0] != e.docs.jwts[1]);

    DocsEnv f;
    f.docs.set(401, json{{"error", "unauthorized"}});
    CHECK_EQ(f.ask("q").text, "Справка недоступна для этого токена.");
    CHECK_EQ(f.docs.questions.size(), 2u); // ровно один повтор
}

TEST(docs_exchange_failures) {
    DocsEnv e;
    e.docs.set(200, answer_body());
    e.tokens.get(fakes::token()); // прогрев кеша wallet-report
    e.auth.invalid();
    // Кеш wallet-report уже есть; для wallet-docs — обмен отклонён.
    CHECK_EQ(e.ask("q").text, "Справка недоступна для этого токена.");
    CHECK(e.docs.questions.empty());

    DocsEnv f;
    f.docs.set(200, answer_body());
    f.tokens.get(fakes::token());
    f.auth.unavailable();
    CHECK_EQ(f.ask("q").text, "Справка временно недоступна, попробуйте позже.");
}

TEST(docs_question_not_logged) {
    fakes::CaptureLog log;
    DocsEnv e;
    e.docs.set(200, answer_body(3));
    mcp::Handlers h;
    h.today = *dates::parse("2026-10-31");
    h.docs_enabled = true;
    h.call_tool = [&](const std::string& name, const json& args) {
        mcpaccess::UserReports ur(e.reports, e.tokens, fakes::token(), e.tokens.get(fakes::token()));
        tools::DocsContext dc{e.docs, e.docs_tokens, mcpaccess::Presented::pat(fakes::token())};
        tools::Context ctx{ur, e.directory, h.today, &dc};
        return tools::call(name, args, ctx);
    };
    mcp::Reply r = mcp::handle(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ask_docs","arguments":{"question":"СЕКРЕТНЫЙ_ВОПРОС"}}})",
        h);
    CHECK_EQ(r.status, 200);
    std::string text = log.text();
    CHECK(text.find("\"tool\":\"ask_docs\"") != std::string::npos);
    CHECK(text.find("\"sources\":3") != std::string::npos);
    CHECK(text.find("СЕКРЕТНЫЙ_ВОПРОС") == std::string::npos);
    CHECK(text.find("Баланс — сумма") == std::string::npos);
}

// --- клиент wallet-docs на настоящем HTTP ---

#include <atomic>
#include <chrono>
#include <thread>

#include "docs_client.hpp"
#include "httplib.h"

namespace {

struct FakeWalletDocs {
    httplib::Server svr;
    std::thread th;
    int port = 0;
    std::atomic<int> status{200};
    std::atomic<int> delay_ms{0};
    std::string body = R"({"answer":"ответ","sources":[]})";
    std::string last_body, last_auth, last_trace;

    FakeWalletDocs() {
        svr.Post("/api/docs/ask", [this](const httplib::Request& req, httplib::Response& res) {
            last_body = req.body;
            last_auth = req.get_header_value("Authorization");
            last_trace = req.get_header_value("X-Trace-Id");
            if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms.load()));
            res.status = status;
            if (status == 429) res.set_header("Retry-After", "54");
            res.set_content(body, "application/json");
        });
        port = svr.bind_to_any_port("127.0.0.1");
        th = std::thread([this] { svr.listen_after_bind(); });
        svr.wait_until_ready();
    }
    ~FakeWalletDocs() {
        svr.stop();
        th.join();
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
};

} // namespace

TEST(docs_client_ok_and_headers) {
    FakeWalletDocs d;
    auto api = docsclient::make_http_api(d.url());
    docsclient::Response r = api->ask("Как это?", "jwt-1");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body["answer"], "ответ");
    CHECK_EQ(d.last_auth, "Bearer jwt-1");
    CHECK_EQ(json::parse(d.last_body)["question"], "Как это?");
}

TEST(docs_client_statuses) {
    FakeWalletDocs d;
    auto api = docsclient::make_http_api(d.url());
    for (int st : {400, 401, 403, 503}) {
        d.status = st;
        d.body = R"({"error":"x"})";
        CHECK_EQ(api->ask("q", "j").status, st);
    }
    d.status = 429;
    docsclient::Response r = api->ask("q", "j");
    CHECK_EQ(r.status, 429);
    CHECK_EQ(r.retry_after, 54);
    d.status = 200;
    d.body = "не json";
    r = api->ask("q", "j");
    CHECK_EQ(r.status, 200);
    CHECK(r.body.is_null());
}

TEST(docs_client_timeout_and_closed_port) {
    FakeWalletDocs d;
    d.delay_ms = 1500;
    auto slow = docsclient::make_http_api(d.url(), 1000, 300);
    CHECK_EQ(slow->ask("q", "j").status, 0);
    auto closed = docsclient::make_http_api("http://127.0.0.1:1", 300, 300);
    docsclient::Response r = closed->ask("q", "j");
    CHECK_EQ(r.status, 0);
    CHECK(!r.error.empty());
}

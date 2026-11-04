// Клиент google-auth на настоящем HTTP: локальный httplib-сервер вместо
// google-auth (200, 401, 500, таймаут, закрытый порт).
#include <atomic>
#include <chrono>
#include <thread>

#include "auth_client.hpp"
#include "fakes.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "test_framework.hpp"

namespace {

struct FakeGoogleAuth {
    httplib::Server svr;
    std::thread th;
    int port = 0;
    std::atomic<int> status{200};
    std::atomic<int> delay_ms{0};
    std::string body = R"({"access_token":"jwt.abc","expires_in":300,"user_id":"u-1","device_id":"d-1"})";
    std::string last_body;
    std::string last_content_type;
    // /internal/token/exchange (RFC 8693)
    std::atomic<int> exchange_status{200};
    std::string exchange_body = R"({"access_token":"jwt.rep","issued_token_type":"urn:ietf:params:oauth:token-type:access_token","token_type":"Bearer","expires_in":240})";
    std::string exchange_error = R"({"error":"invalid_grant"})";
    std::atomic<int> exchange_calls{0};
    httplib::Params last_form;
    std::string last_secret;
    std::string last_exchange_content_type;

    FakeGoogleAuth() {
        svr.Post("/internal/token/exchange", [this](const httplib::Request& req, httplib::Response& res) {
            ++exchange_calls;
            last_form = req.params;
            last_secret = req.get_header_value("X-Service-Secret");
            last_exchange_content_type = req.get_header_value("Content-Type");
            res.status = exchange_status;
            res.set_content(exchange_status == 200 ? exchange_body : exchange_error, "application/json");
        });
        svr.Post("/internal/mcp/token", [this](const httplib::Request& req, httplib::Response& res) {
            last_body = req.body;
            last_content_type = req.get_header_value("Content-Type");
            if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms.load()));
            res.status = status;
            if (status == 200) {
                res.set_content(body, "application/json");
            } else if (status == 401) {
                res.set_content(R"({"error":"invalid_token"})", "application/json");
            } else {
                res.set_content(R"({"error":"boom"})", "application/json");
            }
        });
        port = svr.bind_to_any_port("127.0.0.1");
        th = std::thread([this] { svr.listen_after_bind(); });
        svr.wait_until_ready();
    }
    ~FakeGoogleAuth() {
        svr.stop();
        th.join();
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
};

} // namespace

TEST(auth_client_ok) {
    FakeGoogleAuth ga;
    auto api = auth::make_http_api(ga.url());
    auth::Exchange e = api->exchange(fakes::token());
    CHECK(e.result == auth::Exchange::Result::Ok);
    CHECK_EQ(e.access_token, "jwt.abc");
    CHECK_EQ(e.expires_in, 300);
    CHECK_EQ(e.user_id, "u-1");
    // Тело — ровно {"token": …} (контракт exchangeMcpToken).
    auto sent = nlohmann::json::parse(ga.last_body);
    CHECK_EQ(sent, (nlohmann::json{{"token", fakes::token()}}));
    CHECK(ga.last_content_type.find("application/json") == 0);
}

TEST(auth_client_401_is_invalid) {
    FakeGoogleAuth ga;
    ga.status = 401;
    auto e = auth::make_http_api(ga.url())->exchange(fakes::token());
    CHECK(e.result == auth::Exchange::Result::Invalid);
    CHECK(e.access_token.empty());
}

TEST(auth_client_500_is_unavailable) {
    FakeGoogleAuth ga;
    ga.status = 500;
    auto e = auth::make_http_api(ga.url())->exchange(fakes::token());
    CHECK(e.result == auth::Exchange::Result::Unavailable);
    CHECK(e.error.find("500") != std::string::npos);
}

TEST(auth_client_broken_200_is_unavailable) {
    FakeGoogleAuth ga;
    ga.body = R"({"access_token":"","expires_in":300})";
    CHECK(auth::make_http_api(ga.url())->exchange(fakes::token()).result == auth::Exchange::Result::Unavailable);
    ga.body = "not json";
    CHECK(auth::make_http_api(ga.url())->exchange(fakes::token()).result == auth::Exchange::Result::Unavailable);
}

TEST(auth_client_timeout_is_unavailable) {
    FakeGoogleAuth ga;
    ga.delay_ms = 1500;
    auto started = std::chrono::steady_clock::now();
    auto e = auth::make_http_api(ga.url(), 1000, 500)->exchange(fakes::token());
    auto took = std::chrono::steady_clock::now() - started;
    CHECK(e.result == auth::Exchange::Result::Unavailable);
    CHECK(took < std::chrono::milliseconds(1400));
    // Ошибка для лога — без самого токена.
    CHECK(e.error.find("wmcp_") == std::string::npos);
}

TEST(auth_client_closed_port_is_unavailable) {
    int port = 0;
    {
        FakeGoogleAuth ga;
        port = ga.port;
    }
    auto e = auth::make_http_api("http://127.0.0.1:" + std::to_string(port), 500, 500)->exchange(fakes::token());
    CHECK(e.result == auth::Exchange::Result::Unavailable);
}

// --- /internal/token/exchange: разбор ответа (D4) ---

TEST(auth_parse_exchange_200_ok) {
    auto e = auth::parse_token_exchange_response(
        200, R"({"access_token":"a.b.c","issued_token_type":"urn:ietf:params:oauth:token-type:access_token","token_type":"Bearer","expires_in":300})");
    CHECK(e.result == auth::Exchange::Result::Ok);
    CHECK_EQ(e.access_token, "a.b.c");
    CHECK_EQ(e.expires_in, 300);
    CHECK(!e.invalid_client);
}

TEST(auth_parse_exchange_400_is_invalid) {
    for (const char* code : {"invalid_grant", "invalid_target", "invalid_scope", "invalid_request"}) {
        auto e = auth::parse_token_exchange_response(400, std::string(R"({"error":")") + code + "\"}");
        CHECK(e.result == auth::Exchange::Result::Invalid);
        CHECK(!e.invalid_client);
    }
}

TEST(auth_parse_exchange_401_invalid_client_is_unavailable) {
    auto e = auth::parse_token_exchange_response(401, R"({"error":"invalid_client"})");
    CHECK(e.result == auth::Exchange::Result::Unavailable);
    CHECK(e.invalid_client);
}

TEST(auth_parse_exchange_broken_200_and_5xx_are_unavailable) {
    CHECK(auth::parse_token_exchange_response(200, "not json").result == auth::Exchange::Result::Unavailable);
    CHECK(auth::parse_token_exchange_response(200, R"({"access_token":"","expires_in":300})").result ==
          auth::Exchange::Result::Unavailable);
    CHECK(auth::parse_token_exchange_response(200, R"({"access_token":"x","expires_in":0})").result ==
          auth::Exchange::Result::Unavailable);
    auto down = auth::parse_token_exchange_response(503, R"({"error":"unavailable"})");
    CHECK(down.result == auth::Exchange::Result::Unavailable);
    CHECK(!down.invalid_client);
    CHECK(auth::parse_token_exchange_response(500, "").result == auth::Exchange::Result::Unavailable);
}

TEST(auth_parse_mcp_token_response) {
    CHECK(auth::parse_mcp_token_response(401, "").result == auth::Exchange::Result::Invalid);
    CHECK(auth::parse_mcp_token_response(500, "").result == auth::Exchange::Result::Unavailable);
    auto ok = auth::parse_mcp_token_response(200, R"({"access_token":"t","expires_in":300,"user_id":"u"})");
    CHECK(ok.result == auth::Exchange::Result::Ok);
    CHECK_EQ(ok.user_id, "u");
}

// --- /internal/token/exchange по HTTP ---

TEST(auth_client_exchange_jwt_form_and_secret) {
    FakeGoogleAuth ga;
    auto api = auth::make_http_api(ga.url(), 3000, 5000, "s3cret");
    auth::Exchange e = api->exchange_jwt("eyJ.subject.jwt");
    CHECK(e.result == auth::Exchange::Result::Ok);
    CHECK_EQ(e.access_token, "jwt.rep");
    CHECK_EQ(e.expires_in, 240);
    CHECK_EQ(ga.last_secret, "s3cret");
    CHECK(ga.last_exchange_content_type.find("application/x-www-form-urlencoded") == 0);
    auto field = [&ga](const char* k) {
        auto it = ga.last_form.find(k);
        return it == ga.last_form.end() ? std::string("<нет>") : it->second;
    };
    CHECK_EQ(field("grant_type"), "urn:ietf:params:oauth:grant-type:token-exchange");
    CHECK_EQ(field("subject_token"), "eyJ.subject.jwt");
    CHECK_EQ(field("subject_token_type"), "urn:ietf:params:oauth:token-type:access_token");
    CHECK_EQ(field("audience"), "wallet-report");
    CHECK_EQ(field("scope"), "<нет>");
}

TEST(auth_client_exchange_jwt_errors) {
    FakeGoogleAuth ga;
    auto api = auth::make_http_api(ga.url(), 3000, 5000, "s3cret");
    ga.exchange_status = 400;
    CHECK(api->exchange_jwt("x.y.z").result == auth::Exchange::Result::Invalid);
    ga.exchange_status = 401;
    ga.exchange_error = R"({"error":"invalid_client"})";
    auto e = api->exchange_jwt("x.y.z");
    CHECK(e.result == auth::Exchange::Result::Unavailable);
    CHECK(e.invalid_client);
    ga.exchange_status = 503;
    ga.exchange_error = R"({"error":"unavailable"})";
    CHECK(api->exchange_jwt("x.y.z").result == auth::Exchange::Result::Unavailable);
}

TEST(auth_client_exchange_jwt_without_secret_does_not_call) {
    FakeGoogleAuth ga;
    auto e = auth::make_http_api(ga.url())->exchange_jwt("x.y.z");
    CHECK(e.result == auth::Exchange::Result::Unavailable);
    CHECK_EQ(ga.exchange_calls.load(), 0);
}

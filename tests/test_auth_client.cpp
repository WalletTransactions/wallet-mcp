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

    FakeGoogleAuth() {
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

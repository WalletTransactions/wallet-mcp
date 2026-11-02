#include "auth_client.hpp"

#include "httplib.h"
#include "nlohmann/json.hpp"
#include "util/log.hpp"

using nlohmann::json;

namespace auth {

const char* to_string(Exchange::Result r) {
    switch (r) {
    case Exchange::Result::Ok:
        return "ok";
    case Exchange::Result::Invalid:
        return "invalid";
    case Exchange::Result::Unavailable:
        return "unavailable";
    }
    return "unavailable";
}

namespace {

class HttpAuthApi final : public AuthApi {
public:
    HttpAuthApi(std::string base_url, int connect_timeout_ms, int read_timeout_ms)
        : base_url_(std::move(base_url)), connect_ms_(connect_timeout_ms), read_ms_(read_timeout_ms) {}

    Exchange exchange(const std::string& mcp_token) override {
        httplib::Client client(base_url_);
        client.set_connection_timeout(connect_ms_ / 1000, (connect_ms_ % 1000) * 1000);
        client.set_read_timeout(read_ms_ / 1000, (read_ms_ % 1000) * 1000);
        client.set_write_timeout(read_ms_ / 1000, (read_ms_ % 1000) * 1000);

        httplib::Headers headers;
        std::string trace_id = applog::current_trace_id();
        if (!trace_id.empty()) headers.emplace("X-Trace-Id", trace_id);

        Exchange out;
        auto res = client.Post("/internal/mcp/token", headers, json{{"token", mcp_token}}.dump(), "application/json");
        if (!res) {
            out.error = "google-auth недоступен: " + httplib::to_string(res.error());
            return out;
        }
        if (res->status == 401) {
            out.result = Exchange::Result::Invalid;
            return out;
        }
        if (res->status != 200) {
            out.error = "google-auth ответил HTTP " + std::to_string(res->status);
            return out;
        }
        json body = json::parse(res->body, nullptr, /*allow_exceptions=*/false);
        if (!body.is_object() || !body.contains("access_token") || !body["access_token"].is_string() ||
            !body.contains("expires_in") || !body["expires_in"].is_number_integer()) {
            out.error = "ответ google-auth не по контракту exchangeMcpToken";
            return out;
        }
        out.access_token = body["access_token"].get<std::string>();
        out.expires_in = body["expires_in"].get<int>();
        if (out.access_token.empty() || out.expires_in <= 0) {
            out.access_token.clear();
            out.error = "ответ google-auth не по контракту exchangeMcpToken";
            return out;
        }
        if (body.contains("user_id") && body["user_id"].is_string()) out.user_id = body["user_id"].get<std::string>();
        out.result = Exchange::Result::Ok;
        return out;
    }

private:
    std::string base_url_;
    int connect_ms_;
    int read_ms_;
};

} // namespace

std::unique_ptr<AuthApi> make_http_api(const std::string& base_url, int connect_timeout_ms, int read_timeout_ms) {
    return std::make_unique<HttpAuthApi>(base_url, connect_timeout_ms, read_timeout_ms);
}

} // namespace auth

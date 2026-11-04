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

constexpr const char* kGrantTokenExchange = "urn:ietf:params:oauth:grant-type:token-exchange";
constexpr const char* kTokenTypeAccess = "urn:ietf:params:oauth:token-type:access_token";
constexpr const char* kAudienceReport = "wallet-report";

// Общий для обоих контрактов разбор успешного ответа: access_token и
// expires_in > 0, иначе Unavailable с contract в ошибке.
Exchange parse_ok_body(const std::string& body, const char* contract) {
    Exchange out;
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("access_token") || !j["access_token"].is_string() ||
        !j.contains("expires_in") || !j["expires_in"].is_number_integer()) {
        out.error = std::string("ответ google-auth не по контракту ") + contract;
        return out;
    }
    std::string access_token = j["access_token"].get<std::string>();
    int expires_in = j["expires_in"].get<int>();
    if (access_token.empty() || expires_in <= 0) {
        out.error = std::string("ответ google-auth не по контракту ") + contract;
        return out;
    }
    out.access_token = std::move(access_token);
    out.expires_in = expires_in;
    if (j.contains("user_id") && j["user_id"].is_string()) out.user_id = j["user_id"].get<std::string>();
    out.result = Exchange::Result::Ok;
    return out;
}

std::string error_code(const std::string& body) {
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_object() && j.contains("error") && j["error"].is_string()) return j["error"].get<std::string>();
    return {};
}

class HttpAuthApi final : public AuthApi {
public:
    HttpAuthApi(std::string base_url, int connect_timeout_ms, int read_timeout_ms, std::string exchange_secret)
        : base_url_(std::move(base_url)), connect_ms_(connect_timeout_ms), read_ms_(read_timeout_ms),
          exchange_secret_(std::move(exchange_secret)) {}

    Exchange exchange(const std::string& mcp_token) override {
        auto client = make_client();
        auto res = client->Post("/internal/mcp/token", trace_headers(), json{{"token", mcp_token}}.dump(),
                                "application/json");
        if (!res) return network_error(res.error());
        return parse_mcp_token_response(res->status, res->body);
    }

    Exchange exchange_jwt(const std::string& subject_jwt) override {
        if (exchange_secret_.empty()) {
            Exchange out;
            out.error = "AUTH_EXCHANGE_SECRET не задан";
            return out;
        }
        auto client = make_client();
        httplib::Headers headers = trace_headers();
        headers.emplace("X-Service-Secret", exchange_secret_);
        httplib::Params form{{"grant_type", kGrantTokenExchange},
                             {"subject_token", subject_jwt},
                             {"subject_token_type", kTokenTypeAccess},
                             {"audience", kAudienceReport}};
        auto res = client->Post("/internal/token/exchange", headers, form);
        if (!res) return network_error(res.error());
        return parse_token_exchange_response(res->status, res->body);
    }

private:
    std::unique_ptr<httplib::Client> make_client() const {
        auto client = std::make_unique<httplib::Client>(base_url_);
        client->set_connection_timeout(connect_ms_ / 1000, (connect_ms_ % 1000) * 1000);
        client->set_read_timeout(read_ms_ / 1000, (read_ms_ % 1000) * 1000);
        client->set_write_timeout(read_ms_ / 1000, (read_ms_ % 1000) * 1000);
        return client;
    }

    static httplib::Headers trace_headers() {
        httplib::Headers headers;
        std::string trace_id = applog::current_trace_id();
        if (!trace_id.empty()) headers.emplace("X-Trace-Id", trace_id);
        return headers;
    }

    static Exchange network_error(httplib::Error e) {
        Exchange out;
        out.error = "google-auth недоступен: " + httplib::to_string(e);
        return out;
    }

    std::string base_url_;
    int connect_ms_;
    int read_ms_;
    std::string exchange_secret_;
};

} // namespace

Exchange parse_mcp_token_response(int status, const std::string& body) {
    if (status == 401) {
        Exchange out;
        out.result = Exchange::Result::Invalid;
        return out;
    }
    if (status != 200) {
        Exchange out;
        out.error = "google-auth ответил HTTP " + std::to_string(status);
        return out;
    }
    return parse_ok_body(body, "exchangeMcpToken");
}

Exchange parse_token_exchange_response(int status, const std::string& body) {
    if (status == 200) {
        Exchange out = parse_ok_body(body, "/internal/token/exchange");
        // user_id в ответе обмена нет — sub берётся из проверенного входящего JWT.
        out.user_id.clear();
        return out;
    }
    Exchange out;
    if (status == 400) {
        out.result = Exchange::Result::Invalid;
        return out;
    }
    if (status == 401 && error_code(body) == "invalid_client") {
        out.invalid_client = true;
        out.error = "google-auth: invalid_client — AUTH_EXCHANGE_SECRET не совпадает с секретом wallet-mcp";
        return out;
    }
    out.error = "google-auth ответил на обмен HTTP " + std::to_string(status);
    return out;
}

std::unique_ptr<AuthApi> make_http_api(const std::string& base_url, int connect_timeout_ms, int read_timeout_ms,
                                       std::string exchange_secret) {
    return std::make_unique<HttpAuthApi>(base_url, connect_timeout_ms, read_timeout_ms, std::move(exchange_secret));
}

} // namespace auth

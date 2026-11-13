#include "docs_client.hpp"

#include <cstdlib>

#include "httplib.h"
#include "util/log.hpp"
#include "util/metrics.hpp"

using nlohmann::json;

namespace docsclient {

namespace {

class HttpDocsApi final : public DocsApi {
public:
    HttpDocsApi(std::string base_url, int connect_ms, int read_ms)
        : base_url_(std::move(base_url)), connect_ms_(connect_ms), read_ms_(read_ms) {}

    Response ask(const std::string& question, const std::string& access_token) override {
        httplib::Client client(base_url_);
        client.set_connection_timeout(connect_ms_ / 1000, (connect_ms_ % 1000) * 1000);
        client.set_read_timeout(read_ms_ / 1000, (read_ms_ % 1000) * 1000);
        client.set_write_timeout(connect_ms_ / 1000, (connect_ms_ % 1000) * 1000);
        httplib::Headers headers{{"Authorization", "Bearer " + access_token}};
        std::string trace_id = applog::current_trace_id();
        if (!trace_id.empty()) headers.emplace("X-Trace-Id", trace_id);

        Response out;
        auto res = client.Post("/api/docs/ask", headers, json{{"question", question}}.dump(), "application/json");
        if (!res) {
            out.error = "wallet-docs недоступен: " + httplib::to_string(res.error());
            metrics::inc_counter("mcp_docs_errors_total", "Ошибки вызова wallet-docs по виду.", "kind", "network");
            return out;
        }
        out.status = res->status;
        out.body = json::parse(res->body, nullptr, /*allow_exceptions=*/false);
        if (out.body.is_discarded()) out.body = nullptr;
        std::string retry = res->get_header_value("Retry-After");
        if (!retry.empty()) out.retry_after = std::atoi(retry.c_str());
        if (res->status != 200) {
            out.error = "wallet-docs ответил HTTP " + std::to_string(res->status);
            metrics::inc_counter("mcp_docs_errors_total", "Ошибки вызова wallet-docs по виду.", "kind",
                                 "http_" + std::to_string(res->status));
        }
        return out;
    }

private:
    std::string base_url_;
    int connect_ms_;
    int read_ms_;
};

} // namespace

std::unique_ptr<DocsApi> make_http_api(const std::string& base_url, int connect_timeout_ms, int read_timeout_ms) {
    return std::make_unique<HttpDocsApi>(base_url, connect_timeout_ms, read_timeout_ms);
}

} // namespace docsclient

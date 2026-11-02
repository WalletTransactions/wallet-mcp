// wallet-mcp — MCP-сервер стека (openspec/changes/add-mcp-read-tools):
// MCP-клиент (LM Studio, позже любой) с персональным токеном `wmcp_…`
// вызывает инструменты кошелька по Streamable HTTP за wallet-gateway
// (/api/mcp/). Модель приносит клиент; сервис только исполняет
// детерминированные инструменты.
//
// Инвариант: только чтение. Данные — из wallet-report JWT пользователя,
// который сервис получает обменом MCP-токена в google-auth. Своей базы нет,
// прав на базу кошелька и инструментов записи нет.

#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

#include "access.hpp"
#include "auth_client.hpp"
#include "clients/reports_client.hpp"
#include "dates.hpp"
#include "directory.hpp"
#include "docs.hpp"
#include "httplib.h"
#include "mcp_http.hpp"
#include "nlohmann/json.hpp"
#include "util/dotenv.hpp"
#include "util/log.hpp"
#include "util/metrics.hpp"
#include "version.hpp"

namespace {

std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    return (v != nullptr && *v != '\0') ? std::string(v) : fallback;
}

std::string env_required(const char* key, const std::string& hint) {
    const char* v = std::getenv(key);
    if (v == nullptr || *v == '\0') {
        applog::error(std::string(key) + " must be set — " + hint + " (см. README.md)");
        std::exit(1);
    }
    return std::string(v);
}

std::string strip_trailing_slash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

// trace_id ставит гейтвей — сам сервис новый не порождает.
httplib::Server::HandlerResponse handle_pre_routing(const httplib::Request& req, httplib::Response& res) {
    applog::start_request_timer();
    std::string trace_id = req.get_header_value("X-Trace-Id");
    if (!trace_id.empty()) {
        applog::set_trace_id(trace_id);
        res.set_header("X-Trace-Id", trace_id);
    }
    return httplib::Server::HandlerResponse::Unhandled;
}

} // namespace

int main() {
    dotenv::load(".env");

    std::string log_dir = env_or("LOG_DIR", "");
    if (log_dir == "stdout" || log_dir == "-") log_dir.clear();
    applog::init(log_dir, "wallet-mcp");
    metrics::set_build_info(version::kService, version::kVersion, version::kGitCommit);

    std::string auth_url = strip_trailing_slash(
        env_required("AUTH_SERVICE_URL", "адрес google-auth для обмена MCP-токена, например http://127.0.0.1:8080"));
    std::string report_url = strip_trailing_slash(
        env_required("WALLET_REPORT_URL", "адрес wallet-report, например http://127.0.0.1:8084"));

    // Пояс «сегодня» (design D7) — один на сервис. Выставляется в TZ процесса
    // до первого потока: дальше localtime_r потокобезопасен.
    std::string tz_name = env_or("MCP_TZ", "UTC");
    if (!dates::is_valid_tz(tz_name)) {
        applog::error("MCP_TZ=" + tz_name + " — неизвестный пояс IANA (нужен файл в /usr/share/zoneinfo, например "
                      "Europe/Moscow; см. README.md)");
        return 1;
    }
    setenv("TZ", tz_name.c_str(), 1);
    tzset();

    std::string host = env_or("HOST", "127.0.0.1");
    int port = std::stoi(env_or("PORT", "8088"));

    auto auth_api = auth::make_http_api(auth_url);
    mcpaccess::TokenCache tokens(*auth_api);
    auto reports_api = reports::make_http_api(report_url, "mcp_reports_errors_total");
    directory::Cache directory;
    mcphttp::Deps deps{tokens, *reports_api, directory, tz_name, version::kVersion};

    httplib::Server svr;
    svr.set_pre_routing_handler(handle_pre_routing);
    // Лимит тела (спека mcp-transport): больше 64 КБ — 413 до хендлера, то
    // есть до аутентификации и разбора.
    svr.set_payload_max_length(mcphttp::kMaxBodyBytes);
    mcphttp::register_routes(svr, deps);
    docsapi::register_routes(svr);

    svr.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        std::string message = "internal error";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            message = e.what();
        }
        applog::error("unhandled exception: " + req.method + " " + req.path + " -> " + message);
        res.status = 500;
        res.set_content(nlohmann::json{{"error", "internal server error"}}.dump(), "application/json");
    });

    // Access-лог без тел запросов и ответов: в /api/mcp/ суммы, имена счетов
    // и комментарии (design D13).
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        metrics::observe_http_request(req.method,
                                      applog::current_route().empty() ? "(unset)" : applog::current_route(),
                                      res.status, applog::request_duration_ms());
        if (req.path == "/healthz" || req.path == "/metrics") {
            applog::clear_request_context();
            return;
        }
        applog::info(req.method + " " + req.path + " -> " + std::to_string(res.status),
                     {{"event", "request_finished"}, {"duration_ms", applog::request_duration_ms()}});
        applog::clear_request_context();
    });

    applog::info("wallet-mcp v" + std::string(version::kVersion) + " (" + std::string(version::kGitCommit) +
                 ") listening on http://" + host + ":" + std::to_string(port) + " (google-auth: " + auth_url +
                 ", wallet-report: " + report_url + ", MCP_TZ: " + tz_name + ")");
    if (!svr.listen(host, port)) {
        applog::error("не удалось запустить сервер на " + host + ":" + std::to_string(port) +
                      " (порт занят или недоступен?)");
        return 1;
    }
    return 0;
}

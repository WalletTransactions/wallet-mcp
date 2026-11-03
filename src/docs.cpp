#include "docs.hpp"

#include <fstream>
#include <sstream>

#include "metrics2.hpp"
#include "nlohmann/json.hpp"
#include "util/log.hpp"
#include "util/metrics.hpp"
#include "version.hpp"

namespace docsapi {

namespace {

constexpr const char* kOpenApiPath = "openapi.yaml";
constexpr const char* kVendorDir = "vendor/swagger-ui";

constexpr const char* kDocsHtml = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>wallet-mcp — API docs</title>
<link rel="stylesheet" href="/vendor/swagger-ui/swagger-ui.css">
<link rel="icon" type="image/png" sizes="32x32" href="/vendor/swagger-ui/favicon-32x32.png">
<link rel="icon" type="image/png" sizes="16x16" href="/vendor/swagger-ui/favicon-16x16.png">
<style>body{margin:0;background:#fafafa}</style>
</head>
<body>
<div id="swagger-ui"></div>
<script src="/vendor/swagger-ui/swagger-ui-bundle.js"></script>
<script src="/vendor/swagger-ui/swagger-ui-standalone-preset.js"></script>
<script>
window.onload = function () {
  window.ui = SwaggerUIBundle({
    url: "/openapi.yaml",
    dom_id: "#swagger-ui",
    presets: [SwaggerUIBundle.presets.apis, SwaggerUIStandalonePreset],
    layout: "StandaloneLayout",
    tryItOutEnabled: true
  });
};
</script>
</body>
</html>
)HTML";

void handle_openapi_yaml(const httplib::Request&, httplib::Response& res) {
    applog::set_route("GET /openapi.yaml");
    std::ifstream f(kOpenApiPath, std::ios::binary);
    if (!f) {
        res.status = 500;
        res.set_content("openapi.yaml не найден рядом с исполняемым файлом — сборка не докопировала его "
                        "(см. CMakeLists.txt/Containerfile)",
                        "text/plain; charset=utf-8");
        return;
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    res.set_content(buf.str(), "application/yaml; charset=utf-8");
}

void handle_docs(const httplib::Request&, httplib::Response& res) {
    applog::set_route("GET /docs");
    res.set_content(kDocsHtml, "text/html; charset=utf-8");
}

// Liveness для healthcheck compose: google-auth и wallet-report нарочно не
// проверяются — их сбой не повод перезапускать этот сервис.
void handle_healthz(const httplib::Request&, httplib::Response& res) {
    applog::set_route("GET /healthz");
    res.set_content(R"({"status":"ok"})", "application/json; charset=utf-8");
}

// Prometheus 0.0.4: счётчики/гистограммы util/metrics и счётчики с двумя
// лейблами (metrics2 — mcp_tool_calls_total).
void handle_metrics(const httplib::Request&, httplib::Response& res) {
    applog::set_route("GET /metrics");
    res.set_content(metrics::render() + metrics2::render(), "text/plain; version=0.0.4; charset=utf-8");
}

void handle_version(const httplib::Request&, httplib::Response& res) {
    applog::set_route("GET /version");
    nlohmann::json j;
    j["service"] = version::kService;
    j["version"] = version::kVersion;
    j["build"] = version::kBuild;
    j["git_commit"] = version::kGitCommit;
    j["started_at"] = metrics::started_at_iso();
    res.set_content(j.dump(), "application/json; charset=utf-8");
}

} // namespace

void register_routes(httplib::Server& svr) {
    svr.Get("/openapi.yaml", handle_openapi_yaml);
    svr.Get("/docs", handle_docs);
    svr.Get("/healthz", handle_healthz);
    svr.Get("/metrics", handle_metrics);
    svr.Get("/version", handle_version);
    svr.set_mount_point("/vendor/swagger-ui", kVendorDir);
}

} // namespace docsapi

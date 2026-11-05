#include "mcp.hpp"

#include <chrono>

#include "metrics2.hpp"
#include "util/log.hpp"
#include "util/metrics.hpp"

using nlohmann::json;

namespace mcp {

namespace {

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;

Reply json_reply(const json& j) {
    Reply r;
    r.body = j.dump();
    return r;
}

Reply error_reply(const json& id, int code, const std::string& message) {
    return json_reply({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
}

Reply result_reply(const json& id, json result) {
    return json_reply({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
}

Reply accepted() {
    Reply r;
    r.status = 202;
    return r;
}

bool valid_id(const json& id) { return id.is_string() || id.is_number_integer() || id.is_number_unsigned(); }

Reply call_tool(const json& id, const json& params, const Handlers& h) {
    if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
        return error_reply(id, kInvalidParams, "tools/call: нужен params.name");
    }
    const std::string name = params["name"].get<std::string>();
    if (!tools::exists(name)) return error_reply(id, kInvalidParams, "Неизвестный инструмент: " + name);
    json arguments = params.contains("arguments") && !params["arguments"].is_null() ? params["arguments"] : json::object();

    auto started = std::chrono::steady_clock::now();
    tools::Outcome outcome;
    std::string invalid;
    try {
        outcome = h.call_tool(name, arguments);
    } catch (const tools::InvalidParams& e) {
        invalid = e.what();
        outcome.kind = tools::Outcome::Kind::ToolError;
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    // Имя инструмента — из реестра, поэтому годится как лейбл и поле лога.
    // Аргументы, суммы и текст ответа в лог не пишутся.
    const char* result = tools::to_string(outcome.kind);
    metrics2::inc_counter("mcp_tool_calls_total", "Вызовы инструментов MCP по имени, исходу и виду учётных данных.",
                          {{"tool", name}, {"result", result}, {"credential", h.credential}});
    metrics::observe_duration("mcp_tool_duration_seconds", "Длительность вызова инструмента MCP.", "tool", name,
                              seconds);
    applog::info("tool " + name + " -> " + result, {{"event", "mcp_tool_call"},
                                                     {"tool", name},
                                                     {"result", result},
                                                     {"credential", h.credential},
                                                     {"duration_ms", seconds * 1000.0}});

    if (!invalid.empty()) return error_reply(id, kInvalidParams, invalid);
    return result_reply(id, {{"content", json::array({{{"type", "text"}, {"text", outcome.text}}})},
                             {"isError", outcome.kind != tools::Outcome::Kind::Ok}});
}

} // namespace

const std::vector<std::string>& known_protocol_versions() {
    static const std::vector<std::string> v = {"2025-11-25", "2025-06-18", "2025-03-26"};
    return v;
}

std::string negotiate_version(const std::string& requested) {
    for (const auto& v : known_protocol_versions()) {
        if (v == requested) return v;
    }
    return known_protocol_versions().front();
}

bool protocol_header_ok(const std::string& value) {
    if (value.empty()) return true;
    for (const auto& v : known_protocol_versions()) {
        if (v == value) return true;
    }
    return false;
}

std::string instructions(const dates::Date& today, const std::string& tz_name) {
    // Периоды — уже посчитанные: с примером «сентябрь — 2026-09-01…» маленькая
    // модель на «прошлый месяц» копировала даты примера, а не считала от
    // сегодня.
    dates::Range cur = dates::this_month(today);
    dates::Range prev = dates::previous_month(today);
    auto range = [](const dates::Range& r) {
        return "from " + dates::to_string(r.from) + ", to " + dates::to_string(r.to);
    };
    return "Сегодня " + dates::to_string(today) + ", " + dates::weekday_ru(today) + " (пояс " + tz_name +
           "). Инструменты wallet-mcp читают кошельки, категории и операции пользователя, ничего не меняют.\n"
           "Не считай суммы сам — используй результаты инструментов.\n"
           "Среднее в месяц бери из avg_per_month, сравнение месяцев года (лето, зима) — через months_of_year в "
           "spending_by_category_by_month; сам не дели и не складывай.\n"
           "Периоды передавай явными датами ГГГГ-ММ-ДД, обе границы включаются. Готовые периоды: «этот месяц» (" +
           dates::month_name_ru(cur.from.month) + ") — " + range(cur) + "; «прошлый месяц» (" +
           dates::month_name_ru(prev.from.month) + ") — " + range(prev) +
           ". Месяц по названию без года — последний такой месяц, не позже текущего. «На этой неделе» и другие "
           "периоды считай от сегодняшней даты выше.\n"
           "Имена категорий и кошельков передавай ровно как в списке инструмента, без своих пояснений в скобках.\n"
           "Если инструмент ответил, что имя подходит к нескольким, — переспроси пользователя, какое он имел в "
           "виду, а не выбирай сам.\n"
           "В каждом ответе есть period и currency, по которым он посчитан, — называй их пользователю вместе с "
           "суммой.";
}

Reply handle(const std::string& body, const Handlers& h) {
    json msg = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (msg.is_discarded()) return error_reply(nullptr, kParseError, "Parse error: тело не JSON");
    if (msg.is_array()) return error_reply(nullptr, kInvalidRequest, "Invalid Request: батчи не поддерживаются");
    if (!msg.is_object()) return error_reply(nullptr, kInvalidRequest, "Invalid Request: ожидается объект JSON-RPC");

    bool has_id = msg.contains("id") && !msg["id"].is_null();
    json id = has_id ? msg["id"] : json(nullptr);
    if (has_id && !valid_id(id)) return error_reply(nullptr, kInvalidRequest, "Invalid Request: id — строка или число");
    if (!msg.contains("jsonrpc") || msg["jsonrpc"] != "2.0") {
        return error_reply(id, kInvalidRequest, "Invalid Request: нужен \"jsonrpc\": \"2.0\"");
    }
    if (!msg.contains("method")) {
        // Ответ клиента на запрос сервера (сервер их не шлёт) — принять и забыть.
        if (msg.contains("result") || msg.contains("error")) return accepted();
        return error_reply(id, kInvalidRequest, "Invalid Request: нет method");
    }
    if (!msg["method"].is_string()) return error_reply(id, kInvalidRequest, "Invalid Request: method — строка");
    const std::string method = msg["method"].get<std::string>();
    // Уведомление (без id): notifications/initialized, notifications/cancelled
    // и любые другие — 202 без тела.
    if (!has_id) return accepted();

    json params = msg.contains("params") ? msg["params"] : json::object();
    if (method == "initialize") {
        std::string requested = params.is_object() && params.contains("protocolVersion") &&
                                        params["protocolVersion"].is_string()
                                    ? params["protocolVersion"].get<std::string>()
                                    : "";
        Reply r = result_reply(id, {{"protocolVersion", negotiate_version(requested)},
                                    {"capabilities", {{"tools", json::object()}}},
                                    {"serverInfo", {{"name", "wallet-mcp"}, {"version", h.server_version}}},
                                    {"instructions", instructions(h.today, h.tz_name)}});
        r.headers["Mcp-Session-Id"] = h.new_session_id();
        return r;
    }
    if (method == "ping") return result_reply(id, json::object());
    if (method == "tools/list") return result_reply(id, tools::list());
    if (method == "tools/call") return call_tool(id, params, h);
    return error_reply(id, kMethodNotFound, "Method not found: " + method);
}

} // namespace mcp

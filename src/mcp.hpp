// MCP поверх JSON-RPC 2.0 (спека mcp-transport, design D1, D2): разбор
// одного сообщения уже аутентифицированного запроса и ответ. Без сети и
// без состояния — HTTP, токен и wallet-report снаружи (mcp_http.cpp), чтобы
// диспетчер проверялся юнит-тестами.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "dates.hpp"
#include "tools.hpp"

namespace mcp {

// Ревизии MCP с рукопожатием initialize, которые сервис знает (новые —
// первыми). Первая — «последняя известная»: её получает клиент, запросивший
// незнакомую версию.
const std::vector<std::string>& known_protocol_versions();
std::string negotiate_version(const std::string& requested);

// Текст instructions для initialize: дата и день недели в поясе сервиса и
// правила для модели.
std::string instructions(const dates::Date& today, const std::string& tz_name);

struct Reply {
    int status = 200;
    std::string body; // пусто при 202
    std::map<std::string, std::string> headers;
};

struct Handlers {
    dates::Date today;
    std::string tz_name;
    std::string server_version;
    std::function<std::string()> new_session_id;
    // Вид учётных данных запроса ("pat"/"jwt") — лейбл mcp_tool_calls_total
    // и поле журнала (change add-jwt-access, D5).
    std::string credential = "pat";
    // tools/call: имя уже проверено на существование; InvalidParams из
    // вызова становится -32602, остальные исключения идут наружу.
    std::function<tools::Outcome(const std::string& name, const nlohmann::json& arguments)> call_tool;
};

// body — тело POST /api/mcp/. Ошибки протокола — JSON-RPC-ошибка с HTTP 200;
// уведомление или ответ клиента — 202 без тела.
Reply handle(const std::string& body, const Handlers& h);

// Значение заголовка MCP-Protocol-Version: пусто или знакомая версия — ок.
bool protocol_header_ok(const std::string& value);

} // namespace mcp

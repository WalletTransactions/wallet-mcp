// Инструменты кошелька только для чтения (спека wallet-tools, design
// D5–D12): реестр со схемами для tools/list, проверка аргументов и сами
// вызовы wallet-report токеном пользователя.
//
// Два слоя ошибок (D5): аргумент не по схеме — InvalidParams (JSON-RPC
// -32602, видит клиент); ошибка смысла (дата не существует, имя не найдено,
// wallet-report отказал) — Outcome с isError и текстом для модели.
#pragma once

#include <stdexcept>
#include <string>

#include "access.hpp"
#include "dates.hpp"
#include "directory.hpp"
#include "nlohmann/json.hpp"

namespace tools {

extern const char* const kReportsUnavailable;
extern const char* const kNoWallets;

struct InvalidParams : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

struct Outcome {
    enum class Kind { Ok, ToolError, Unavailable };
    Kind kind = Kind::Ok;
    std::string text; // content[0].text: компактный JSON или текст ошибки
};

const char* to_string(Outcome::Kind k);

struct Context {
    mcpaccess::UserReports& reports;
    directory::Cache& directory;
    dates::Date today; // в поясе MCP_TZ
};

// Результат tools/list: {"tools":[{name, description, inputSchema}, ...]}.
const nlohmann::json& list();

bool exists(const std::string& name);

// «Baby Sitter (Няня)» → «Baby Sitter»: пояснение в скобках в конце имени
// (модель дописывает перевод) отрезается. Без скобок в конце — как есть.
std::string without_parenthetical(const std::string& name);

// Проверка аргументов по inputSchema инструмента: неизвестный инструмент,
// лишний ключ, неверный тип, нет обязательного, вне enum/диапазона —
// InvalidParams. null у необязательного аргумента — как его отсутствие.
void validate(const std::string& name, const nlohmann::json& arguments);

// validate + вызов. InvalidParams, mcpaccess::AuthRejected и
// mcpaccess::AuthUnavailable пробрасываются наружу.
Outcome call(const std::string& name, const nlohmann::json& arguments, Context& ctx);

} // namespace tools

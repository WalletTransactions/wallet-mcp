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
#include "docs_client.hpp"
#include "nlohmann/json.hpp"

namespace tools {

extern const char* const kReportsUnavailable;
extern const char* const kNoWallets;
// Тексты отказов ask_docs — дословно из спеки docs-lookup.
extern const char* const kDocsUnavailable;
extern const char* const kDocsForbidden;
extern const char* const kDocsEmptyQuestion;

struct InvalidParams : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

struct Outcome {
    enum class Kind { Ok, ToolError, Unavailable };
    Kind kind = Kind::Ok;
    std::string text; // content[0].text: компактный JSON или текст ошибки
    int sources = -1; // ask_docs: число источников в ответе; -1 — не применимо
};

const char* to_string(Outcome::Kind k);

// Справка (change docs-tool): клиент wallet-docs, отдельный кеш JWT для него и
// предъявленные учётные данные запроса. Нет в Context — инструмента нет.
struct DocsContext {
    docsclient::DocsApi& api;
    mcpaccess::TokenCache& tokens; // кеш с audience wallet-docs
    mcpaccess::Presented presented;
};

struct Context {
    mcpaccess::UserReports& reports;
    directory::Cache& directory;
    dates::Date today; // в поясе MCP_TZ
    DocsContext* docs = nullptr;
};

// Результат tools/list: {"tools":[{name, description, inputSchema}, ...]}.
// ask_docs — только при docs_enabled (задан WALLET_DOCS_URL).
nlohmann::json list(bool docs_enabled = false);

bool exists(const std::string& name, bool docs_enabled = false);

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

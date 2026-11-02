#include "tools.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "clients/reports_client.hpp"
#include "decimal.hpp"
#include "expense/text.hpp"
#include "util/log.hpp"

using nlohmann::json;
using ojson = nlohmann::ordered_json;

namespace tools {

const char* const kReportsUnavailable = "Отчёты временно недоступны, попробуйте позже.";
const char* const kNoWallets = "Кошельков пока нет — заведите их в приложении.";

const char* to_string(Outcome::Kind k) {
    switch (k) {
    case Outcome::Kind::Ok:
        return "ok";
    case Outcome::Kind::ToolError:
        return "tool_error";
    case Outcome::Kind::Unavailable:
        return "unavailable";
    }
    return "ok";
}

namespace {

// ---------------------------------------------------------------------------
// Схемы. Описания — на русском: их читает модель клиента.

constexpr int kMaxNames = 50;     // столько uuid wallet-report принимает в фильтре
constexpr int kMaxTextChars = 100; // q у /operations
constexpr int kDefaultLimit = 20;
constexpr std::size_t kCommentBytes = 120;

json date_prop(const std::string& description) {
    return {{"type", "string"}, {"format", "date"}, {"description", description}};
}

json names_prop(const std::string& description) {
    return {{"type", "array"},
            {"items", {{"type", "string"}}},
            {"minItems", 1},
            {"maxItems", kMaxNames},
            {"description", description}};
}

json currency_prop() {
    return {{"type", "string"},
            {"description", "ISO-код валюты эквивалента для сумм, например RUB. По умолчанию — валюта первого "
                            "кошелька; фактическая валюта всегда есть в ответе (currency)"}};
}

json tool(const std::string& name, const std::string& description, json properties,
          std::vector<std::string> required = {}) {
    json schema = {{"type", "object"}, {"properties", std::move(properties)}, {"additionalProperties", false}};
    if (!required.empty()) schema["required"] = required;
    return {{"name", name}, {"description", description}, {"inputSchema", std::move(schema)}};
}

json build_registry() {
    json from = date_prop("Начало периода, ГГГГ-ММ-ДД, день включается");
    json to = date_prop("Конец периода, ГГГГ-ММ-ДД, день включается");
    json tools = json::array();
    tools.push_back(tool("list_wallets",
                         "Кошельки пользователя (без архивных): имя, валюта и баланс на конец сегодняшнего дня в "
                         "валюте кошелька, и общий итог total в валюте currency (посчитан сервером).",
                         {{"currency", currency_prop()}}));
    tools.push_back(tool("list_categories",
                         "Имена категорий пользователя (без системных и архивных) — точные имена для аргументов "
                         "categories других инструментов.",
                         json::object()));
    tools.push_back(tool(
        "spending_by_category",
        "Расход по категориям за период: сумма каждой категории (amount) и итог total — их сумма, посчитанная "
        "сервером. categories — имена категорий (можно начало имени); без них — все категории с расходом за "
        "период и total всего расхода. С compare_from и compare_to — то же за период сравнения: amount_before, "
        "delta = amount − amount_before и total_before.",
        {{"from", from},
         {"to", to},
         {"categories", names_prop("Имена категорий, например [\"Няня\", \"Продукты\"]")},
         {"compare_from", date_prop("Начало периода сравнения, ГГГГ-ММ-ДД — только вместе с compare_to")},
         {"compare_to", date_prop("Конец периода сравнения, ГГГГ-ММ-ДД, включительно — только вместе с compare_from")},
         {"currency", currency_prop()}},
        {"from", "to"}));
    tools.push_back(tool("income_expense_by_month",
                         "Доход (income) и расход (expense) по каждому календарному месяцу периода, включая месяцы "
                         "без операций (с нулями).",
                         {{"from", from}, {"to", to}, {"currency", currency_prop()}}, {"from", "to"}));
    tools.push_back(tool("balance",
                         "Баланс на конец дня date (по умолчанию — сегодня): по всем кошелькам или только по "
                         "кошельку wallet. Баланс кошелька — в его валюте, итог total — в валюте currency.",
                         {{"wallet", {{"type", "string"}, {"description", "Имя кошелька (можно начало имени)"}}},
                          {"date", date_prop("День, ГГГГ-ММ-ДД: баланс на его конец")},
                          {"currency", currency_prop()}}));
    tools.push_back(tool(
        "search_operations",
        "Поиск операций за период по комментарию (text), категориям, кошелькам и типу kind. Возвращает до limit "
        "последних операций (has_more: true — есть ещё) и total — сумму ВСЕХ подходящих операций периода, "
        "посчитанную сервером, и count — их число. kind по умолчанию \"expense\": total — сколько потрачено, "
        "неотрицательное. kind \"income\" и \"transfer\" — сумма доходов или переводов. kind \"any\" — все "
        "типы, тогда total — сальдо: доходы минус расходы, прямые переводы между кошельками в него не входят; "
        "при \"any\" без других фильтров total нет.",
        {{"from", from},
         {"to", to},
         {"text", {{"type", "string"},
                   {"maxLength", kMaxTextChars},
                   {"description", "Подстрока комментария, без учёта регистра, до 100 символов"}}},
         {"categories", names_prop("Имена категорий")},
         {"wallets", names_prop("Имена кошельков")},
         {"kind", {{"type", "string"},
                   {"enum", {"expense", "income", "transfer", "any"}},
                   {"description", "Тип операций, по умолчанию expense"}}},
         {"limit", {{"type", "integer"},
                    {"minimum", 1},
                    {"maximum", 50},
                    {"description", "Сколько операций показать, 1..50, по умолчанию 20"}}},
         {"currency", currency_prop()}},
        {"from", "to"}));
    return {{"tools", tools}};
}

const json& registry() {
    static const json r = build_registry();
    return r;
}

const json* schema_of(const std::string& name) {
    for (const auto& t : registry()["tools"]) {
        if (t["name"] == name) return &t["inputSchema"];
    }
    return nullptr;
}

std::size_t utf8_chars(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

void check_value(const std::string& key, const json& value, const json& schema) {
    const std::string type = schema["type"];
    auto bad = [&](const std::string& why) { throw InvalidParams("Аргумент " + key + ": " + why); };
    if (type == "string") {
        if (!value.is_string()) bad("ожидается строка");
        if (schema.contains("enum")) {
            bool found = false;
            for (const auto& e : schema["enum"]) found = found || e == value;
            if (!found) bad("допустимы только " + schema["enum"].dump());
        }
        if (schema.contains("maxLength") && utf8_chars(value.get<std::string>()) > schema["maxLength"].get<std::size_t>()) {
            bad("не длиннее " + schema["maxLength"].dump() + " символов");
        }
    } else if (type == "integer") {
        bool integral = value.is_number_integer() ||
                        (value.is_number_float() && value.get<double>() == static_cast<double>(static_cast<long long>(value.get<double>())));
        if (!integral) bad("ожидается целое число");
        long long v = value.is_number_integer() ? value.get<long long>() : static_cast<long long>(value.get<double>());
        if (schema.contains("minimum") && v < schema["minimum"].get<long long>()) bad("не меньше " + schema["minimum"].dump());
        if (schema.contains("maximum") && v > schema["maximum"].get<long long>()) bad("не больше " + schema["maximum"].dump());
    } else if (type == "array") {
        if (!value.is_array()) bad("ожидается массив строк");
        if (schema.contains("minItems") && value.size() < schema["minItems"].get<std::size_t>()) bad("пустой массив");
        if (schema.contains("maxItems") && value.size() > schema["maxItems"].get<std::size_t>()) {
            bad("не больше " + schema["maxItems"].dump() + " элементов");
        }
        for (const auto& item : value) {
            if (!item.is_string()) bad("ожидается массив строк");
        }
    }
}

// ---------------------------------------------------------------------------
// Вызов.

struct ToolFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct ReportsDown : std::runtime_error {
    ReportsDown() : std::runtime_error("wallet-report unavailable") {}
};

bool has(const json& args, const char* key) { return args.contains(key) && !args[key].is_null(); }

std::string str_arg(const json& args, const char* key) { return has(args, key) ? args[key].get<std::string>() : ""; }

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& s : items) out += (out.empty() ? "" : ", ") + s;
    return out;
}

std::string query(const std::string& path, const std::vector<std::pair<std::string, std::string>>& params) {
    std::string out = path;
    char sep = '?';
    for (const auto& [k, v] : params) {
        out += sep + k + "=" + reports::url_encode(v);
        sep = '&';
    }
    return out;
}

struct Period {
    dates::Date from;
    dates::Date to;
    ojson echo() const { return {{"from", dates::to_string(from)}, {"to", dates::to_string(to)}}; }
};

struct Eq {
    std::string iso;
    int precision = 2;
};

dates::Date date_arg(const json& args, const char* key) {
    std::string raw = str_arg(args, key);
    auto d = dates::parse(raw);
    if (!d) {
        throw ToolFailure(std::string("Дата ") + key + " \"" + raw +
                          "\" неверна: нужен существующий день в формате ГГГГ-ММ-ДД.");
    }
    return *d;
}

Period period_arg(const json& args, const char* from_key, const char* to_key) {
    Period p{date_arg(args, from_key), date_arg(args, to_key)};
    if (p.to < p.from) {
        throw ToolFailure(std::string("Период неверен: ") + from_key + " (" + dates::to_string(p.from) + ") позже " +
                          to_key + " (" + dates::to_string(p.to) + ").");
    }
    return p;
}

// Суммы ответа: строка wallet-report по precision валюты, null — нет курса.
ojson amount(const json& v, int precision) {
    if (v.is_string()) return decimal::format_string(v.get<std::string>(), precision);
    return nullptr;
}

std::optional<decimal::Decimal> dec(const json& v) {
    if (!v.is_string()) return std::nullopt;
    return decimal::parse(v.get<std::string>());
}

class Call {
public:
    explicit Call(Context& ctx) : ctx_(ctx) {}

    json fetch(const std::string& path_and_query) {
        reports::Response r = ctx_.reports.get(path_and_query);
        if (r.status != 200 || !r.body.is_object()) {
            // В лог — только путь без query: в query даты и текст поиска.
            std::string path = path_and_query.substr(0, path_and_query.find('?'));
            applog::warn("wallet-report не ответил: " + path,
                         {{"event", "mcp_reports_unavailable"}, {"path", path}, {"status", r.status}});
            throw ReportsDown();
        }
        return std::move(r.body);
    }

    const resolver::Accounts& accounts() {
        if (!accounts_) {
            auto cached = ctx_.directory.accounts(ctx_.reports.key());
            if (cached) {
                accounts_ = std::move(*cached);
            } else {
                load_accounts();
            }
        }
        return *accounts_;
    }

    text::Named category(const std::string& name) { return resolve(name, false); }
    text::Named wallet(const std::string& name) { return resolve(name, true); }

    Eq currency(const json& args) {
        const auto& a = accounts();
        if (a.wallets.empty()) throw ToolFailure(kNoWallets);
        if (!has(args, "currency")) return {a.wallets.front().iso_code, a.wallets.front().precision};
        std::string raw = args["currency"].get<std::string>();
        std::string iso = trim(raw);
        for (auto& ch : iso) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const auto& list = currencies();
        for (const auto& c : list) {
            if (c.iso_code == iso) return {c.iso_code, c.precision};
        }
        std::vector<std::string> shown;
        for (const auto& c : list) {
            if (c.show_in_equivalent) shown.push_back(c.iso_code);
        }
        throw ToolFailure("Валюта \"" + raw + "\" не найдена. Есть: " + join(shown) + ".");
    }

    // Валюта кошелька по его uuid (у строк /operations только символ).
    std::string wallet_iso(const std::string& wallet_uuid) {
        for (const auto& w : accounts().wallets) {
            if (w.uuid == wallet_uuid) return w.iso_code;
        }
        return {};
    }

private:
    void load_accounts() {
        json body = fetch("/api/reports/accounts");
        if (!body.contains("accounts") || !body["accounts"].is_array()) throw ReportsDown();
        accounts_ = resolver::parse_accounts(body);
        ctx_.directory.put_accounts(ctx_.reports.key(), *accounts_);
        fresh_ = true;
    }

    const std::vector<directory::Currency>& currencies() {
        if (!currencies_) {
            auto cached = ctx_.directory.currencies(ctx_.reports.key());
            if (cached) {
                currencies_ = std::move(*cached);
            } else {
                auto parsed = directory::parse_currencies(fetch("/api/reports/currencies"));
                if (!parsed) throw ReportsDown();
                currencies_ = std::move(*parsed);
                ctx_.directory.put_currencies(ctx_.reports.key(), *currencies_);
            }
        }
        return *currencies_;
    }

    std::vector<text::Named> named(bool wallets) {
        std::vector<text::Named> out;
        if (wallets) {
            for (const auto& w : accounts().wallets) out.push_back({w.uuid, w.name});
        } else {
            for (const auto& c : accounts().categories) out.push_back({c.uuid, c.name});
        }
        return out;
    }

    text::Named resolve(const std::string& name, bool wallets) {
        text::Match m = text::resolve_name(name, named(wallets));
        if (m.kind == text::Match::Kind::NotFound && !fresh_) {
            // Категорию или кошелёк могли только что завести в приложении.
            ctx_.directory.drop_accounts(ctx_.reports.key());
            load_accounts();
            m = text::resolve_name(name, named(wallets));
        }
        if (m.kind == text::Match::Kind::Found) return m.found;
        if (m.kind == text::Match::Kind::Ambiguous) {
            std::vector<std::string> names;
            for (const auto& c : m.candidates) names.push_back(c.name);
            throw ToolFailure("\"" + name + "\" подходит к нескольким: " + join(names) + ". Уточните.");
        }
        std::vector<std::string> all;
        for (const auto& n : named(wallets)) all.push_back(n.name);
        if (all.empty()) {
            throw ToolFailure(wallets ? std::string(kNoWallets)
                                      : "\"" + name + "\" не найдено. Категорий пока нет — заведите их в приложении.");
        }
        throw ToolFailure("\"" + name + "\" не найдено. Есть: " + join(all) + ".");
    }

    Context& ctx_;
    std::optional<resolver::Accounts> accounts_;
    std::optional<std::vector<directory::Currency>> currencies_;
    bool fresh_ = false; // справочник уже загружен в этом вызове
};

std::vector<text::Named> resolve_all(Call& c, const json& args, const char* key, bool wallets) {
    std::vector<text::Named> out;
    if (!has(args, key)) return out;
    std::unordered_set<std::string> seen;
    for (const auto& item : args[key]) {
        text::Named n = wallets ? c.wallet(item.get<std::string>()) : c.category(item.get<std::string>());
        if (seen.insert(n.uuid).second) out.push_back(n);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Инструменты.

ojson list_categories(Call& c, const json&) {
    ojson names = ojson::array();
    for (const auto& cat : c.accounts().categories) names.push_back(cat.name);
    return {{"categories", names}};
}

// list_wallets и balance: /balance?date=<день после> (граница не включается).
ojson wallets_balance(Call& c, const json& args, const dates::Date& day) {
    Eq eq = c.currency(args);
    std::optional<text::Named> one;
    if (has(args, "wallet")) one = c.wallet(args["wallet"].get<std::string>());

    json body = c.fetch(query("/api/reports/balance", {{"date", dates::balance_date_end_of(day)}, {"eq_currency", eq.iso}}));
    if (!body.contains("accounts") || !body["accounts"].is_array()) throw ReportsDown();
    std::unordered_map<std::string, const json*> rows;
    for (const auto& r : body["accounts"]) {
        if (r.is_object() && r.contains("account_uuid") && r["account_uuid"].is_string()) {
            rows[r["account_uuid"].get<std::string>()] = &r;
        }
    }

    ojson wallets = ojson::array();
    ojson total;
    bool incomplete = false;
    for (const auto& w : c.accounts().wallets) {
        if (one && w.uuid != one->uuid) continue;
        auto it = rows.find(w.uuid);
        json bal = it != rows.end() && it->second->contains("balance") ? (*it->second)["balance"] : json("0");
        wallets.push_back({{"name", w.name}, {"currency", w.iso_code}, {"balance", amount(bal, w.precision)}});
        if (one) {
            json bal_eq = it != rows.end() && it->second->contains("balance_eq") ? (*it->second)["balance_eq"] : json("0");
            total = amount(bal_eq, eq.precision);
            incomplete = total.is_null();
        }
    }
    if (!one) {
        // Итог — total_eq wallet-report: в нём и архивные кошельки, как в вебе.
        total = amount(body.value("total_eq", json("0")), eq.precision);
        incomplete = body.value("incomplete", false);
    }
    return {{"date", dates::to_string(day)},
            {"currency", eq.iso},
            {"wallets", wallets},
            {"total", total},
            {"incomplete", incomplete}};
}

ojson list_wallets(Call& c, const json& args, const Context& ctx) { return wallets_balance(c, args, ctx.today); }

ojson balance(Call& c, const json& args, const Context& ctx) {
    dates::Date day = has(args, "date") ? date_arg(args, "date") : ctx.today;
    return wallets_balance(c, args, day);
}

struct Structure {
    std::vector<std::pair<std::string, std::string>> order; // uuid, имя — в порядке ответа
    std::unordered_map<std::string, json> amount_eq;         // uuid -> строка или null
    json total_eq;
    bool incomplete = false;
};

Structure expense_structure(Call& c, const Period& p, const Eq& eq) {
    json body = c.fetch(query("/api/reports/expense-structure", {{"from", dates::period_from(p.from)},
                                                                 {"to", dates::period_to(p.to)},
                                                                 {"eq_currency", eq.iso}}));
    if (!body.contains("categories") || !body["categories"].is_array()) throw ReportsDown();
    Structure s;
    for (const auto& r : body["categories"]) {
        if (!r.is_object() || !r.contains("account_uuid") || !r["account_uuid"].is_string()) continue;
        std::string uuid = r["account_uuid"].get<std::string>();
        s.order.emplace_back(uuid, r.value("account_name", ""));
        s.amount_eq[uuid] = r.contains("amount_eq") ? r["amount_eq"] : json(nullptr);
    }
    s.total_eq = body.value("total_eq", json("0"));
    s.incomplete = body.value("incomplete", false);
    return s;
}

ojson spending_by_category(Call& c, const json& args) {
    Period p = period_arg(args, "from", "to");
    std::optional<Period> cmp;
    if (has(args, "compare_from") != has(args, "compare_to")) {
        throw ToolFailure("Задайте обе границы периода сравнения — compare_from и compare_to — или ни одной.");
    }
    if (has(args, "compare_from")) cmp = period_arg(args, "compare_from", "compare_to");
    Eq eq = c.currency(args);
    std::vector<text::Named> selected = resolve_all(c, args, "categories", false);

    Structure now = expense_structure(c, p, eq);
    // Сравнение — вторым вызовом без cmp_*: ответ с cmp_* даёт доли, но не
    // суммы категорий в периоде сравнения (D10).
    std::optional<Structure> before;
    if (cmp) before = expense_structure(c, *cmp, eq);

    std::vector<std::pair<std::string, std::string>> rows_order;
    if (!selected.empty()) {
        for (const auto& n : selected) rows_order.emplace_back(n.uuid, n.name);
    } else {
        std::unordered_set<std::string> seen;
        for (const auto& r : now.order) {
            if (seen.insert(r.first).second) rows_order.push_back(r);
        }
        if (before) {
            for (const auto& r : before->order) {
                if (seen.insert(r.first).second) rows_order.push_back(r);
            }
        }
    }

    auto value_of = [](const Structure& s, const std::string& uuid) {
        auto it = s.amount_eq.find(uuid);
        return it == s.amount_eq.end() ? json("0") : it->second;
    };

    bool incomplete = false;
    decimal::Decimal total{}, total_before{};
    ojson rows = ojson::array();
    for (const auto& [uuid, name] : rows_order) {
        json a = value_of(now, uuid);
        ojson row = {{"category", name}, {"amount", amount(a, eq.precision)}};
        auto da = dec(a);
        if (da) {
            total = decimal::add(total, *da);
        } else {
            incomplete = true;
        }
        if (before) {
            json b = value_of(*before, uuid);
            auto db = dec(b);
            row["amount_before"] = amount(b, eq.precision);
            row["delta"] = da && db ? ojson(decimal::format(decimal::sub(*da, *db), eq.precision)) : ojson(nullptr);
            if (db) {
                total_before = decimal::add(total_before, *db);
            } else {
                incomplete = true;
            }
        }
        rows.push_back(std::move(row));
    }

    ojson out = {{"period", p.echo()}};
    if (cmp) out["compare_period"] = cmp->echo();
    out["currency"] = eq.iso;
    out["rows"] = rows;
    if (!selected.empty()) {
        out["total"] = decimal::format(total, eq.precision);
        if (before) out["total_before"] = decimal::format(total_before, eq.precision);
    } else {
        // Все категории — итог периода из SQL (total_eq), а не сумма строк.
        out["total"] = amount(now.total_eq, eq.precision);
        if (before) out["total_before"] = amount(before->total_eq, eq.precision);
        incomplete = now.incomplete || (before && before->incomplete);
    }
    out["incomplete"] = incomplete;
    return out;
}

ojson income_expense_by_month(Call& c, const json& args) {
    Period p = period_arg(args, "from", "to");
    Eq eq = c.currency(args);
    json body = c.fetch(query("/api/reports/income-expense-by-month", {{"from", dates::period_from(p.from)},
                                                                       {"to", dates::period_to(p.to)},
                                                                       {"eq_currency", eq.iso}}));
    if (!body.contains("months") || !body["months"].is_array()) throw ReportsDown();
    ojson months = ojson::array();
    for (const auto& m : body["months"]) {
        if (!m.is_object()) continue;
        std::string start = m.value("month_start", "");
        months.push_back({{"month", start.substr(0, 7)},
                          {"income", amount(m.value("income_eq", json("0")), eq.precision)},
                          {"expense", amount(m.value("expense_eq", json("0")), eq.precision)}});
    }
    return {{"period", p.echo()}, {"currency", eq.iso}, {"months", months}, {"incomplete", body.value("incomplete", false)}};
}

std::string op_date(const std::string& raw) {
    std::string d = raw;
    std::replace(d.begin(), d.end(), 'T', ' ');
    return d.substr(0, 16);
}

ojson search_operations(Call& c, const json& args) {
    Period p = period_arg(args, "from", "to");
    std::string kind = has(args, "kind") ? args["kind"].get<std::string>() : "expense";
    std::string q = trim(str_arg(args, "text"));
    int limit = has(args, "limit") ? static_cast<int>(args["limit"].get<double>()) : kDefaultLimit;
    Eq eq = c.currency(args);
    std::vector<text::Named> cats = resolve_all(c, args, "categories", false);
    std::vector<text::Named> wallets = resolve_all(c, args, "wallets", true);

    auto uuids = [](const std::vector<text::Named>& v) {
        std::string out;
        for (const auto& n : v) out += (out.empty() ? "" : ",") + n.uuid;
        return out;
    };
    std::vector<std::pair<std::string, std::string>> params = {{"from", dates::period_from(p.from)},
                                                               {"to", dates::period_to(p.to)},
                                                               {"eq_currency", eq.iso},
                                                               {"limit", std::to_string(limit)}};
    if (!cats.empty()) params.emplace_back("category_uuid", uuids(cats));
    if (!wallets.empty()) params.emplace_back("wallet_uuid", uuids(wallets));
    if (kind != "any") params.emplace_back("kind", kind);
    if (!q.empty()) params.emplace_back("q", q);
    bool filtered = !cats.empty() || !wallets.empty() || kind != "any" || !q.empty();

    json body = c.fetch(query("/api/reports/operations", params));
    if (!body.contains("operations") || !body["operations"].is_array()) throw ReportsDown();
    // Итог при фильтре — обязательная часть ответа wallet-report: без него
    // фильтры не применились бы, и показанные строки были бы не те.
    if (filtered && !body.contains("total_eq")) throw ReportsDown();

    ojson ops = ojson::array();
    for (const auto& o : body["operations"]) {
        if (!o.is_object()) continue;
        std::string okind = o.value("kind", "");
        int precision = o.contains("precision") && o["precision"].is_number_integer() ? o["precision"].get<int>() : 2;
        std::string wallet_uuid = okind == "transfer" ? o.value("from_uuid", "") : o.value("wallet_uuid", "");
        std::string iso = c.wallet_iso(wallet_uuid);
        if (iso.empty()) iso = o.value("symbol", "");
        ojson row = {{"date", op_date(o.value("date", ""))},
                     {"kind", okind},
                     {"amount", amount(o.value("amount", json("0")), precision)},
                     {"currency", iso}};
        if (okind == "transfer") {
            row["from"] = o.value("from_name", "");
            row["to"] = o.value("to_name", "");
        } else {
            row["category"] = o.value("category_name", "");
            row["wallet"] = o.value("wallet_name", "");
        }
        std::string comment = o.value("comment", "");
        if (!comment.empty()) {
            // truncate_utf8 добавляет «…» (3 байта) — режем с запасом.
            row["comment"] = comment.size() > kCommentBytes ? text::truncate_utf8(comment, kCommentBytes - 3) : comment;
        }
        ops.push_back(std::move(row));
    }

    ojson out = {{"period", p.echo()}, {"currency", eq.iso}, {"kind", kind}, {"operations", ops},
                 {"has_more", body.value("has_more", false)}};
    if (filtered) {
        out["total"] = amount(body["total_eq"], eq.precision);
        out["count"] = body.value("total_count", 0);
        out["incomplete"] = body.value("total_incomplete", false);
    }
    return out;
}

} // namespace

const json& list() { return registry(); }

bool exists(const std::string& name) { return schema_of(name) != nullptr; }

void validate(const std::string& name, const json& arguments) {
    const json* schema = schema_of(name);
    if (schema == nullptr) throw InvalidParams("Неизвестный инструмент: " + name);
    if (!arguments.is_object()) throw InvalidParams("arguments должен быть объектом");
    const json& props = (*schema)["properties"];
    for (const auto& [key, value] : arguments.items()) {
        if (!props.contains(key)) throw InvalidParams("Неизвестный аргумент: " + key);
        if (value.is_null()) continue;
        check_value(key, value, props[key]);
    }
    if (schema->contains("required")) {
        for (const auto& key : (*schema)["required"]) {
            const std::string k = key.get<std::string>();
            if (!arguments.contains(k) || arguments[k].is_null()) throw InvalidParams("Нет обязательного аргумента: " + k);
        }
    }
}

Outcome call(const std::string& name, const json& arguments, Context& ctx) {
    validate(name, arguments);
    Call c(ctx);
    Outcome out;
    try {
        ojson result;
        if (name == "list_wallets") {
            result = list_wallets(c, arguments, ctx);
        } else if (name == "list_categories") {
            result = list_categories(c, arguments);
        } else if (name == "spending_by_category") {
            result = spending_by_category(c, arguments);
        } else if (name == "income_expense_by_month") {
            result = income_expense_by_month(c, arguments);
        } else if (name == "balance") {
            result = balance(c, arguments, ctx);
        } else if (name == "search_operations") {
            result = search_operations(c, arguments);
        }
        out.kind = Outcome::Kind::Ok;
        out.text = result.dump();
    } catch (const ToolFailure& e) {
        out.kind = Outcome::Kind::ToolError;
        out.text = e.what();
    } catch (const ReportsDown&) {
        out.kind = Outcome::Kind::Unavailable;
        out.text = kReportsUnavailable;
    } catch (const json::exception& e) {
        // Поле ответа wallet-report не того типа — ответ не по контракту.
        applog::warn(std::string("ответ wallet-report не по контракту: ") + e.what(),
                     {{"event", "mcp_reports_unavailable"}, {"tool", name}});
        out.kind = Outcome::Kind::Unavailable;
        out.text = kReportsUnavailable;
    }
    return out;
}

} // namespace tools

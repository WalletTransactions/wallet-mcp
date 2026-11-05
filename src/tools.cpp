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
    json exclude = names_prop("Имена категорий, которые не учитывать: в расчёт идут все остальные. Не вместе с "
                              "categories");
    tools.push_back(tool(
        "spending_by_category",
        "Итоги расхода по категориям за период: сумма каждой категории (amount), число операций (count) и "
        "среднее в месяц (avg_per_month), итог total — их сумма, посчитанная сервером, и months — сколько месяцев "
        "в периоде (делитель среднего). categories — имена категорий (можно начало имени); без них — все "
        "категории с расходом за период и total всего расхода; exclude_categories — все, кроме этих. С "
        "compare_from и compare_to — то же за период сравнения: amount_before, delta = amount − amount_before, "
        "total_before, count_before, avg_per_month_before и months_before; периоды разной длины сравнивай по "
        "avg_per_month.",
        {{"from", from},
         {"to", to},
         {"categories", names_prop("Имена категорий, например [\"Няня\", \"Продукты\"]")},
         {"exclude_categories", exclude},
         {"compare_from", date_prop("Начало периода сравнения, ГГГГ-ММ-ДД — только вместе с compare_to")},
         {"compare_to", date_prop("Конец периода сравнения, ГГГГ-ММ-ДД, включительно — только вместе с compare_from")},
         {"currency", currency_prop()}},
        {"from", "to"}));
    tools.push_back(tool(
        "spending_by_category_by_month",
        "Средние в месяц, расход по месяцам и сезонность по категориям. У каждой категории сумма за период "
        "(amount), число операций (count) и среднее в месяц (avg_per_month); в ответе итог total, count, months — "
        "сколько месяцев в расчёте (неполные месяцы — по доле дней) и avg_per_month итога, всё посчитано "
        "сервером. С categories у строк есть by_month — суммы по непустым месяцам «ГГГГ-ММ». Период from..to — "
        "непрерывный, не длиннее 36 месяцев; months_of_year — фильтр месяцев года внутри периода: лето — [6, 7, "
        "8], остальные месяцы — вторым вызовом с тем же периодом.",
        {{"from", from},
         {"to", to},
         {"categories", names_prop("Имена категорий; с ними в ответе есть by_month")},
         {"exclude_categories", exclude},
         {"months_of_year", {{"type", "array"},
                             {"items", {{"type", "integer"}, {"minimum", 1}, {"maximum", 12}}},
                             {"minItems", 1},
                             {"maxItems", 12},
                             {"uniqueItems", true},
                             {"description", "Номера месяцев года 1..12 без повторов, например [6, 7, 8] — только "
                                             "июни, июли и августы периода"}}},
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
        "Поиск операций за период по комментарию (text), категориям (categories или все, кроме "
        "exclude_categories), кошелькам и типу kind. Возвращает до limit "
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
         {"exclude_categories", exclude},
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
        const bool ints = schema["items"]["type"] == "integer";
        const char* expected = ints ? "ожидается массив целых чисел" : "ожидается массив строк";
        if (!value.is_array()) bad(expected);
        if (schema.contains("minItems") && value.size() < schema["minItems"].get<std::size_t>()) bad("пустой массив");
        if (schema.contains("maxItems") && value.size() > schema["maxItems"].get<std::size_t>()) {
            bad("не больше " + schema["maxItems"].dump() + " элементов");
        }
        for (const auto& item : value) {
            if (ints) {
                if (!item.is_number()) bad(expected);
                check_value(key, item, schema["items"]);
            } else if (!item.is_string()) {
                bad(expected);
            }
        }
        if (schema.value("uniqueItems", false)) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                for (std::size_t j = i + 1; j < value.size(); ++j) {
                    if (value[i] == value[j]) bad("значения не должны повторяться");
                }
            }
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
        // Модель дописывает к имени свой перевод: «Baby Sitter (Няня)». Имя
        // со скобками целиком уже не нашлось — пробуем без пояснения.
        if (m.kind == text::Match::Kind::NotFound) {
            std::string base = without_parenthetical(name);
            if (!base.empty() && base != name) {
                text::Match b = text::resolve_name(base, named(wallets));
                if (b.kind != text::Match::Kind::NotFound) m = b;
            }
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

// Аргументы categories и exclude_categories вместе — ошибка до wallet-report (D5).
void check_category_lists(const json& args) {
    if (has(args, "categories") && has(args, "exclude_categories")) {
        throw ToolFailure("Задайте categories или exclude_categories, не оба.");
    }
}

// Делитель среднего: доли месяцев по дням, в сотых (D3).
struct Months {
    long hundredths = 0;
    std::string text() const { return dates::hundredths_to_string(hundredths); }
    decimal::Decimal value() const { return *decimal::parse(text()); }
};

Months months_of(const std::vector<dates::MonthShare>& shares) { return {dates::months_hundredths(shares)}; }

// avg_per_month = сумма / months (уже округлённый), по precision валюты;
// null — суммы нет (курса не нашлось) или деление невозможно.
ojson average(const std::optional<decimal::Decimal>& sum, const Months& m, int precision) {
    if (!sum) return nullptr;
    auto v = decimal::div(*sum, m.value(), precision);
    return v ? ojson(*v) : ojson(nullptr);
}

// op_count wallet-report: целое; нет поля (wallet-report до
// add-category-month-analytics) — nullopt, в ответе null.
std::optional<long long> op_count(const json& row) {
    if (!row.is_object() || !row.contains("op_count")) return std::nullopt;
    const json& v = row["op_count"];
    if (v.is_number_integer()) return v.get<long long>();
    if (v.is_string()) {
        const std::string raw = v.get<std::string>();
        if (!raw.empty() && std::all_of(raw.begin(), raw.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
            return std::stoll(raw);
        }
    }
    throw ReportsDown(); // поле есть, но не число — ответ не по контракту
}

ojson count_json(const std::optional<long long>& n) { return n ? ojson(*n) : ojson(nullptr); }

std::unordered_set<std::string> uuid_set(const std::vector<text::Named>& v) {
    std::unordered_set<std::string> out;
    for (const auto& n : v) out.insert(n.uuid);
    return out;
}

struct Structure {
    std::vector<std::pair<std::string, std::string>> order;       // uuid, имя — в порядке ответа
    std::unordered_map<std::string, json> amount_eq;               // uuid -> строка или null
    std::unordered_map<std::string, std::optional<long long>> ops; // uuid -> op_count
    bool has_op_count = true; // у всех строк ответа есть op_count (пустой ответ — тоже)
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
        s.ops[uuid] = op_count(r);
        if (!s.ops[uuid]) s.has_op_count = false;
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
    check_category_lists(args);
    Eq eq = c.currency(args);
    std::vector<text::Named> selected = resolve_all(c, args, "categories", false);
    std::unordered_set<std::string> excluded = uuid_set(resolve_all(c, args, "exclude_categories", false));

    Structure now = expense_structure(c, p, eq);
    // Сравнение — вторым вызовом без cmp_*: ответ с cmp_* даёт доли, но не
    // суммы категорий в периоде сравнения (D10).
    std::optional<Structure> before;
    if (cmp) before = expense_structure(c, *cmp, eq);

    std::vector<std::pair<std::string, std::string>> rows_order;
    if (!selected.empty()) {
        for (const auto& n : selected) rows_order.emplace_back(n.uuid, n.name);
    } else {
        std::unordered_set<std::string> seen = excluded;
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
    // Категории нет в ответе — трат не было, 0; строка без op_count — null.
    auto count_of = [](const Structure& s, const std::string& uuid) -> std::optional<long long> {
        auto it = s.ops.find(uuid);
        if (it != s.ops.end()) return it->second;
        return s.has_op_count ? std::optional<long long>(0) : std::nullopt;
    };

    const Months months = months_of(dates::month_fractions(p.from, p.to));
    std::optional<Months> months_before;
    if (cmp) months_before = months_of(dates::month_fractions(cmp->from, cmp->to));

    bool incomplete = false;
    decimal::Decimal total{}, total_before{};
    std::optional<long long> count = 0, count_before = 0;
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
        std::optional<decimal::Decimal> db;
        std::optional<long long> nb;
        if (before) {
            json b = value_of(*before, uuid);
            db = dec(b);
            row["amount_before"] = amount(b, eq.precision);
            row["delta"] = da && db ? ojson(decimal::format(decimal::sub(*da, *db), eq.precision)) : ojson(nullptr);
            if (db) {
                total_before = decimal::add(total_before, *db);
            } else {
                incomplete = true;
            }
            nb = count_of(*before, uuid);
            count_before = count_before && nb ? std::optional<long long>(*count_before + *nb) : std::nullopt;
        }
        // Новые ключи — после прежних: порядок старых полей не меняется (D7).
        std::optional<long long> n = count_of(now, uuid);
        count = count && n ? std::optional<long long>(*count + *n) : std::nullopt;
        row["count"] = count_json(n);
        row["avg_per_month"] = average(da, months, eq.precision);
        if (before) {
            row["count_before"] = count_json(nb);
            row["avg_per_month_before"] = average(db, *months_before, eq.precision);
        }
        rows.push_back(std::move(row));
    }

    ojson out = {{"period", p.echo()}};
    if (cmp) out["compare_period"] = cmp->echo();
    out["currency"] = eq.iso;
    out["rows"] = rows;
    std::optional<decimal::Decimal> sum, sum_before;
    if (!selected.empty() || !excluded.empty()) {
        // Выбранные или «все, кроме» — точная сумма строк ответа.
        out["total"] = decimal::format(total, eq.precision);
        sum = total;
        if (before) {
            out["total_before"] = decimal::format(total_before, eq.precision);
            sum_before = total_before;
        }
    } else {
        // Все категории — итог периода из SQL (total_eq), а не сумма строк.
        out["total"] = amount(now.total_eq, eq.precision);
        sum = dec(now.total_eq);
        if (before) {
            out["total_before"] = amount(before->total_eq, eq.precision);
            sum_before = dec(before->total_eq);
        }
        incomplete = now.incomplete || (before && before->incomplete);
    }
    out["incomplete"] = incomplete;
    out["count"] = count_json(count);
    out["months"] = months.text();
    out["avg_per_month"] = average(sum, months, eq.precision);
    if (before) {
        out["count_before"] = count_json(count_before);
        out["months_before"] = months_before->text();
        out["avg_per_month_before"] = average(sum_before, *months_before, eq.precision);
    }
    return out;
}

// ---------------------------------------------------------------------------
// spending_by_category_by_month (change add-category-analytics).

constexpr int kMaxMonths = 36;

struct MonthCell {
    json amount_eq; // строка или null
    std::optional<long long> count;
};

struct MatrixRow {
    std::string uuid;
    std::string name;
    bool has_total = false; // у категории есть свои total_eq и op_count из SQL
    json total_eq;
    std::optional<long long> count;
    std::vector<std::pair<std::string, MonthCell>> months; // "ГГГГ-ММ" -> клетка, в порядке ответа
};

// "2026-06-01" / "2026-06" → "2026-06"; иначе пусто.
std::string month_key(const json& v) {
    if (!v.is_string()) return {};
    std::string raw = v.get<std::string>();
    if (raw.size() < 7 || raw[4] != '-') return {};
    return raw.substr(0, 7);
}

ojson spending_by_category_by_month(Call& c, const json& args) {
    Period p = period_arg(args, "from", "to");
    check_category_lists(args);
    if (dates::months_touched(p.from, p.to) > kMaxMonths) {
        throw ToolFailure("Период — не длиннее 36 месяцев. Разбейте его на части.");
    }
    std::vector<int> moy;
    if (has(args, "months_of_year")) {
        for (const auto& m : args["months_of_year"]) moy.push_back(static_cast<int>(m.get<double>()));
        std::sort(moy.begin(), moy.end());
    }
    auto month_selected = [&](int month) { return moy.empty() || std::binary_search(moy.begin(), moy.end(), month); };

    std::vector<dates::MonthShare> shares;
    std::unordered_set<std::string> selected_keys; // "ГГГГ-ММ" выбранных месяцев периода
    for (const auto& sh : dates::month_fractions(p.from, p.to)) {
        if (!month_selected(sh.month)) continue;
        shares.push_back(sh);
        selected_keys.insert(dates::to_string({sh.year, sh.month, 1}).substr(0, 7));
    }
    if (shares.empty()) throw ToolFailure("В периоде нет выбранных месяцев.");
    const Months months = months_of(shares);

    Eq eq = c.currency(args);
    std::vector<text::Named> selected = resolve_all(c, args, "categories", false);
    std::unordered_set<std::string> excluded = uuid_set(resolve_all(c, args, "exclude_categories", false));

    json body = c.fetch(query("/api/reports/expense-by-category-by-month", {{"from", dates::period_from(p.from)},
                                                                            {"to", dates::period_to(p.to)},
                                                                            {"eq_currency", eq.iso}}));
    if (!body.contains("categories") || !body["categories"].is_array()) throw ReportsDown();
    std::vector<MatrixRow> matrix;
    for (const auto& r : body["categories"]) {
        if (!r.is_object() || !r.contains("account_uuid") || !r["account_uuid"].is_string()) continue;
        MatrixRow row{r["account_uuid"].get<std::string>(), r.value("account_name", ""),
                      // Итог категории — amount_eq из SQL (как у /expense-structure).
                      r.contains("amount_eq") && r.contains("op_count"), r.value("amount_eq", json(nullptr)),
                      op_count(r), {}};
        if (r.contains("months") && r["months"].is_array()) {
            for (const auto& m : r["months"]) {
                std::string key = m.is_object() && m.contains("month_start") ? month_key(m["month_start"]) : "";
                if (key.empty()) throw ReportsDown();
                row.months.emplace_back(key, MonthCell{m.contains("amount_eq") ? m["amount_eq"] : json(nullptr), op_count(m)});
            }
        }
        matrix.push_back(std::move(row));
    }

    std::unordered_map<std::string, const MatrixRow*> by_uuid;
    for (const auto& r : matrix) by_uuid[r.uuid] = &r;

    // Строки ответа: выбранные (и без трат), все кроме исключённых или все.
    std::vector<std::pair<std::string, std::string>> rows_order;
    if (!selected.empty()) {
        for (const auto& n : selected) rows_order.emplace_back(n.uuid, n.name);
    } else {
        for (const auto& r : matrix) {
            if (!excluded.count(r.uuid)) rows_order.emplace_back(r.uuid, r.name);
        }
    }

    const bool filtered = !selected.empty() || !excluded.empty() || !moy.empty();
    bool incomplete = false;
    decimal::Decimal total{};
    std::optional<long long> count = 0;
    ojson rows = ojson::array();
    for (const auto& [uuid, name] : rows_order) {
        auto it = by_uuid.find(uuid);
        const MatrixRow* src = it == by_uuid.end() ? nullptr : it->second;
        std::optional<decimal::Decimal> sum = decimal::Decimal{};
        std::optional<long long> n = 0;
        ojson by_month = ojson::object();
        bool any_cell = false;
        // Без фильтра месяцев — итог категории из SQL; с фильтром — точная
        // сумма выбранных клеток (D1, D2).
        const bool from_sql = src != nullptr && moy.empty() && src->has_total;
        if (from_sql) {
            sum = dec(src->total_eq);
            n = src->count;
        }
        if (src != nullptr) {
            for (const auto& [key, cell] : src->months) {
                if (!selected_keys.count(key)) continue;
                any_cell = true;
                auto d = dec(cell.amount_eq);
                if (!from_sql) {
                    sum = sum && d ? std::optional<decimal::Decimal>(decimal::add(*sum, *d)) : std::nullopt;
                    n = n && cell.count ? std::optional<long long>(*n + *cell.count) : std::nullopt;
                }
                if (!selected.empty() && !(d && d->units == 0)) by_month[key] = amount(cell.amount_eq, eq.precision);
            }
        }
        // Без categories — только категории с расходом в выбранных месяцах.
        if (selected.empty() && !moy.empty() && !any_cell) continue;

        ojson row = {{"category", name},
                     {"amount", sum ? ojson(decimal::format(*sum, eq.precision)) : ojson(nullptr)},
                     {"count", count_json(n)},
                     {"avg_per_month", average(sum, months, eq.precision)}};
        if (!selected.empty()) row["by_month"] = by_month;
        if (sum) {
            total = decimal::add(total, *sum);
        } else {
            incomplete = true;
        }
        count = count && n ? std::optional<long long>(*count + *n) : std::nullopt;
        rows.push_back(std::move(row));
    }

    ojson out = {{"period", p.echo()}};
    if (!moy.empty()) out["months_of_year"] = moy;
    out["currency"] = eq.iso;
    out["rows"] = rows;
    std::optional<decimal::Decimal> sum_total;
    if (filtered) {
        out["total"] = decimal::format(total, eq.precision);
        sum_total = total;
    } else {
        // Все категории всех месяцев — итог периода из SQL.
        json t = body.value("total_eq", json("0"));
        out["total"] = amount(t, eq.precision);
        sum_total = dec(t);
        incomplete = body.value("incomplete", false);
    }
    out["count"] = count_json(count);
    out["months"] = months.text();
    out["avg_per_month"] = average(sum_total, months, eq.precision);
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
    check_category_lists(args);
    std::string kind = has(args, "kind") ? args["kind"].get<std::string>() : "expense";
    std::string q = trim(str_arg(args, "text"));
    int limit = has(args, "limit") ? static_cast<int>(args["limit"].get<double>()) : kDefaultLimit;
    Eq eq = c.currency(args);
    std::vector<text::Named> cats = resolve_all(c, args, "categories", false);
    std::vector<text::Named> excluded = resolve_all(c, args, "exclude_categories", false);
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
    if (!excluded.empty()) params.emplace_back("exclude_category_uuid", uuids(excluded));
    if (!wallets.empty()) params.emplace_back("wallet_uuid", uuids(wallets));
    if (kind != "any") params.emplace_back("kind", kind);
    if (!q.empty()) params.emplace_back("q", q);
    bool filtered = !cats.empty() || !excluded.empty() || !wallets.empty() || kind != "any" || !q.empty();

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

std::string without_parenthetical(const std::string& name) {
    std::size_t end = name.find_last_not_of(" \t");
    if (end == std::string::npos || name[end] != ')') return name;
    std::size_t open = name.rfind('(', end);
    if (open == std::string::npos || open == 0) return name; // «(Няня)» целиком — не пояснение
    std::size_t last = name.find_last_not_of(" \t", open - 1);
    if (last == std::string::npos) return name;
    return name.substr(0, last + 1);
}

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
        } else if (name == "spending_by_category_by_month") {
            result = spending_by_category_by_month(c, arguments);
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

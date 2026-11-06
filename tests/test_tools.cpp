// Инструменты на фейковом wallet-report (fakes::FakeReports).
#include <cstdio>

#include "decimal.hpp"
#include "fakes.hpp"
#include "nlohmann/json.hpp"
#include "test_framework.hpp"
#include "tools.hpp"

using nlohmann::json;

namespace {

struct Env {
    fakes::FakeAuth auth;
    fakes::FakeReports reports;
    fakes::ManualClock clock;
    mcpaccess::TokenCache tokens{auth};
    directory::Cache directory{clock.fn()};

    Env() { reports.set("/api/reports/accounts", fakes::accounts_body()); }

    tools::Outcome call(const std::string& name, const json& args) {
        mcpaccess::UserReports ur(reports, tokens, fakes::token(), tokens.get(fakes::token()));
        tools::Context ctx{ur, directory, *dates::parse("2026-10-31")};
        return tools::call(name, args, ctx);
    }
    json ok(const std::string& name, const json& args) {
        tools::Outcome o = call(name, args);
        if (o.kind != tools::Outcome::Kind::Ok) {
            ::testing::report_failure("ожидался ok, получено: " + o.text, __FILE__, __LINE__);
            return json::object();
        }
        return json::parse(o.text);
    }
    std::string error(const std::string& name, const json& args) {
        tools::Outcome o = call(name, args);
        if (o.kind != tools::Outcome::Kind::ToolError) {
            ::testing::report_failure("ожидался tool_error, получено: " + o.text, __FILE__, __LINE__);
        }
        return o.text;
    }
};

bool contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

json structure(std::vector<json> rows, const char* total, bool incomplete = false) {
    return {{"categories", rows}, {"total_eq", total}, {"incomplete", incomplete}, {"compared", false},
            {"eq_currency_uuid", "c-RUB"}};
}

json cat_row(const char* uuid, const char* name, json amount) {
    return {{"account_uuid", uuid}, {"account_name", name}, {"amount_eq", amount}, {"icon_sha256", "x"}, {"pct", "1"}};
}

const json kSeptember = {{"from", "2026-09-01"}, {"to", "2026-09-30"}};

// Строки ответа только с прежними ключами: count/avg_per_month (change
// add-category-analytics) проверяются отдельными тестами.
json only(const json& rows, std::vector<std::string> keys) {
    json out = json::array();
    for (const auto& r : rows) {
        json o = json::object();
        for (const auto& k : keys) {
            if (r.contains(k)) o[k] = r[k];
        }
        out.push_back(o);
    }
    return out;
}
const std::vector<std::string> kOldKeys = {"category", "amount", "amount_before", "delta"};

json with(json base, const json& extra) {
    for (const auto& [k, v] : extra.items()) base[k] = v;
    return base;
}

} // namespace

TEST(tools_list_categories_hides_system) {
    Env e;
    json r = e.ok("list_categories", json::object());
    CHECK_EQ(r["categories"], (json{"Няня", "Продукты", "Проезд", "Кафе", "Кафе на работе"}));
}

TEST(tools_accounts_cached_60s) {
    Env e;
    e.ok("list_categories", json::object());
    e.clock.advance(std::chrono::seconds(59));
    e.ok("list_categories", json::object());
    CHECK_EQ(e.reports.count("/api/reports/accounts"), 1);
    e.clock.advance(std::chrono::seconds(2));
    e.ok("list_categories", json::object());
    CHECK_EQ(e.reports.count("/api/reports/accounts"), 2);
}

TEST(tools_list_wallets) {
    Env e;
    e.reports.set("/api/reports/balance",
                  {{"accounts",
                    {{{"account_uuid", "w-cash"}, {"account_name", "Наличные"}, {"currency_uuid", "c-RUB"},
                      {"symbol", "₽"}, {"precision", 2}, {"balance", "1500.5"}, {"balance_eq", "1500.5"}},
                     {{"account_uuid", "w-archived"}, {"account_name", "Старый"}, {"currency_uuid", "c-RUB"},
                      {"symbol", "₽"}, {"precision", 2}, {"balance", "100"}, {"balance_eq", "100"}}}},
                   {"total_eq", "1600.50"},
                   {"incomplete", true},
                   {"eq_currency_uuid", "c-RUB"}});
    json r = e.ok("list_wallets", json::object());
    // Баланс на конец сегодняшнего дня — /balance с завтрашней датой.
    CHECK(contains(e.reports.last("/api/reports/balance"), "date=2026-11-01"));
    CHECK(contains(e.reports.last("/api/reports/balance"), "eq_currency=RUB"));
    CHECK_EQ(r["date"], "2026-10-31");
    CHECK_EQ(r["currency"], "RUB");
    CHECK_EQ(r["wallets"], (json{{{"name", "Наличные"}, {"currency", "RUB"}, {"balance", "1500.50"}},
                                 {{"name", "Revolut"}, {"currency", "EUR"}, {"balance", "0.00"}}}));
    CHECK_EQ(r["total"], "1600.50");
    CHECK_EQ(r["incomplete"], true);
}

TEST(tools_default_currency_is_first_wallet) {
    Env e;
    e.reports.set("/api/reports/expense-structure", structure({}, "0"));
    json r = e.ok("spending_by_category", kSeptember);
    CHECK_EQ(r["currency"], "RUB");
    CHECK(contains(e.reports.last("/api/reports/expense-structure"), "eq_currency=RUB"));
    CHECK_EQ(e.reports.count("/api/reports/currencies"), 0);
}

TEST(tools_explicit_currency_checked_by_directory) {
    Env e;
    e.reports.set("/api/reports/currencies", fakes::currencies_body());
    e.reports.set("/api/reports/expense-structure", structure({cat_row("c-food", "Продукты", "1234.5")}, "1234.5"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"currency", "jpy"}}));
    CHECK_EQ(r["currency"], "JPY");
    CHECK_EQ(r["total"], "1235"); // precision JPY — 0
    std::string err = e.error("spending_by_category", with(kSeptember, {{"currency", "XYZ"}}));
    CHECK_EQ(err, "Валюта \"XYZ\" не найдена. Есть: EUR, JPY, RUB.");
    // Справочник валют — тоже из кеша.
    CHECK_EQ(e.reports.count("/api/reports/currencies"), 1);
}

TEST(tools_no_wallets) {
    Env e;
    e.reports.set("/api/reports/accounts", {{"accounts", json::array()}});
    CHECK_EQ(e.error("list_wallets", json::object()), tools::kNoWallets);
    CHECK_EQ(e.error("spending_by_category", kSeptember), tools::kNoWallets);
    CHECK_EQ(e.reports.count("/api/reports/balance"), 0);
}

TEST(tools_spending_selected_categories) {
    Env e;
    e.reports.set("/api/reports/expense-structure",
                  structure({cat_row("c-nanny", "Няня", "32000.00"), cat_row("c-food", "Продукты", "0.10"),
                             cat_row("c-cafe", "Кафе", "0.20"), cat_row("c-ride", "Проезд", "1999.99")},
                            "34000.29"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"categories", {"няня", "прод", "Кафе"}}}));
    CHECK_EQ(r["period"], (json{{"from", "2026-09-01"}, {"to", "2026-09-30"}}));
    CHECK_EQ(only(r["rows"], kOldKeys), (json{{{"category", "Няня"}, {"amount", "32000.00"}},
                              {{"category", "Продукты"}, {"amount", "0.10"}},
                              {{"category", "Кафе"}, {"amount", "0.20"}}}));
    CHECK_EQ(r["total"], "32000.30");
    CHECK_EQ(r["incomplete"], false);
    CHECK(!r.contains("compare_period"));
    // Границы периода — включительно, по контракту wallet-report.
    std::string q = e.reports.last("/api/reports/expense-structure");
    CHECK(contains(q, "from=2026-09-01%2000%3A00%3A00"));
    CHECK(contains(q, "to=2026-09-30%2023%3A59%3A59.999999"));
}

TEST(tools_spending_kopecks_total) {
    Env e;
    e.reports.set("/api/reports/expense-structure",
                  structure({cat_row("c-nanny", "Няня", "0.10"), cat_row("c-food", "Продукты", "0.20"),
                             cat_row("c-cafe", "Кафе", "1999.99")},
                            "2000.29"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"categories", {"Няня", "Продукты", "Кафе"}}}));
    CHECK_EQ(r["total"], "2000.29");
}

TEST(tools_spending_category_without_expense_and_no_rate) {
    Env e;
    e.reports.set("/api/reports/expense-structure",
                  structure({cat_row("c-nanny", "Няня", "500"), cat_row("c-cafe", "Кафе", nullptr)}, "500", true));
    json r = e.ok("spending_by_category", with(kSeptember, {{"categories", {"Няня", "Продукты", "Кафе"}}}));
    CHECK_EQ(only(r["rows"], kOldKeys)[1], (json{{"category", "Продукты"}, {"amount", "0.00"}}));
    CHECK(r["rows"][2]["amount"].is_null());
    CHECK_EQ(r["total"], "500.00");
    CHECK_EQ(r["incomplete"], true);
}

TEST(tools_spending_all_categories_total_from_sql) {
    Env e;
    e.reports.set("/api/reports/expense-structure",
                  structure({cat_row("c-nanny", "Няня", "100"), cat_row("c-food", "Продукты", "50")}, "150.00"));
    json r = e.ok("spending_by_category", kSeptember);
    CHECK_EQ(r["rows"].size(), 2u);
    CHECK_EQ(r["total"], "150.00");
}

TEST(tools_spending_compare_two_calls) {
    Env e;
    e.reports.push("/api/reports/expense-structure",
                   structure({cat_row("c-food", "Продукты", "12000.50"), cat_row("c-nanny", "Няня", "30000")},
                             "42000.50"));
    e.reports.push("/api/reports/expense-structure",
                   structure({cat_row("c-food", "Продукты", "10000.25"), cat_row("c-cafe", "Кафе", "700")}, "10700.25"));
    json r = e.ok("spending_by_category",
                  with(kSeptember, {{"categories", {"Продукты"}}, {"compare_from", "2026-08-01"}, {"compare_to", "2026-08-31"}}));
    CHECK_EQ(e.reports.count("/api/reports/expense-structure"), 2);
    // Сравнение — без cmp_*, обычным вызовом за август.
    std::string second = e.reports.last("/api/reports/expense-structure");
    CHECK(contains(second, "from=2026-08-01"));
    CHECK(!contains(second, "cmp_"));
    CHECK_EQ(r["compare_period"], (json{{"from", "2026-08-01"}, {"to", "2026-08-31"}}));
    CHECK_EQ(only(r["rows"], kOldKeys), (json{{{"category", "Продукты"}, {"amount", "12000.50"}, {"amount_before", "10000.25"},
                               {"delta", "2000.25"}}}));
    CHECK_EQ(r["total"], "12000.50");
    CHECK_EQ(r["total_before"], "10000.25");
}

TEST(tools_spending_compare_all_categories) {
    Env e;
    e.reports.push("/api/reports/expense-structure", structure({cat_row("c-food", "Продукты", "100")}, "100"));
    e.reports.push("/api/reports/expense-structure", structure({cat_row("c-cafe", "Кафе", "40")}, "40"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"compare_from", "2026-08-01"}, {"compare_to", "2026-08-31"}}));
    CHECK_EQ(only(r["rows"], kOldKeys), (json{{{"category", "Продукты"}, {"amount", "100.00"}, {"amount_before", "0.00"}, {"delta", "100.00"}},
                              {{"category", "Кафе"}, {"amount", "0.00"}, {"amount_before", "40.00"}, {"delta", "-40.00"}}}));
    CHECK_EQ(r["total_before"], "40.00");
}

TEST(tools_spending_period_errors_do_not_call_reports) {
    Env e;
    std::string err = e.error("spending_by_category", {{"from", "2026-09-30"}, {"to", "2026-09-01"}});
    CHECK_EQ(err, "Период неверен: from (2026-09-30) позже to (2026-09-01).");
    err = e.error("spending_by_category", {{"from", "2026-02-30"}, {"to", "2026-03-01"}});
    CHECK_EQ(err, "Дата from \"2026-02-30\" неверна: нужен существующий день в формате ГГГГ-ММ-ДД.");
    err = e.error("spending_by_category", with(kSeptember, {{"compare_from", "2026-08-01"}}));
    CHECK(contains(err, "compare_from и compare_to"));
    CHECK(e.reports.requests.empty());
}

TEST(tools_names_ambiguous_and_not_found) {
    Env e;
    e.reports.set("/api/reports/expense-structure", structure({}, "0"));
    std::string err = e.error("spending_by_category", with(kSeptember, {{"categories", {"каф"}}}));
    CHECK_EQ(err, "\"каф\" подходит к нескольким: Кафе, Кафе на работе. Уточните.");
    CHECK_EQ(e.reports.count("/api/reports/expense-structure"), 0);

    // Ненайденное имя — справочник перечитывается один раз.
    int before = e.reports.count("/api/reports/accounts");
    err = e.error("spending_by_category", with(kSeptember, {{"categories", {"Такси"}}}));
    CHECK_EQ(err, "\"Такси\" не найдено. Есть: Няня, Продукты, Проезд, Кафе, Кафе на работе.");
    CHECK_EQ(e.reports.count("/api/reports/accounts") - before, 1);

    // Системная категория не участвует.
    err = e.error("spending_by_category", with(kSeptember, {{"categories", {"Transfer"}}}));
    CHECK(contains(err, "\"Transfer\" не найдено."));
}

TEST(tools_name_created_moments_ago_found_after_reload) {
    Env e;
    e.ok("list_categories", json::object()); // справочник в кеше
    json acc = fakes::accounts_body();
    acc["accounts"].push_back({{"uuid", "c-taxi"}, {"name", "Такси"}, {"kind", "category"}, {"system_kind", 0},
                               {"is_archived", false}, {"icon_sha256", ""}, {"currency_uuid", ""}, {"iso_code", ""},
                               {"symbol", ""}, {"precision", ""}});
    e.reports.set("/api/reports/accounts", acc);
    e.reports.set("/api/reports/expense-structure", structure({cat_row("c-taxi", "Такси", "450")}, "450"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"categories", {"такси"}}}));
    CHECK_EQ(r["total"], "450.00");
}

TEST(tools_income_expense_by_month) {
    Env e;
    e.reports.set("/api/reports/income-expense-by-month",
                  {{"months",
                    {{{"month_start", "2026-06-01"}, {"income_eq", "100000"}, {"expense_eq", "80000.5"}},
                     {{"month_start", "2026-07-01"}, {"income_eq", "0.00"}, {"expense_eq", "0.00"}}}},
                   {"incomplete", false},
                   {"eq_currency_uuid", "c-RUB"}});
    json r = e.ok("income_expense_by_month", {{"from", "2026-06-01"}, {"to", "2026-07-31"}});
    CHECK_EQ(r["months"], (json{{{"month", "2026-06"}, {"income", "100000.00"}, {"expense", "80000.50"}},
                                {{"month", "2026-07"}, {"income", "0.00"}, {"expense", "0.00"}}}));
    CHECK_EQ(r["period"]["to"], "2026-07-31");
    CHECK_EQ(r["incomplete"], false);
}

TEST(tools_balance_one_wallet_end_of_day) {
    Env e;
    e.reports.set("/api/reports/balance",
                  {{"accounts",
                    {{{"account_uuid", "w-rev"}, {"account_name", "Revolut"}, {"currency_uuid", "c-EUR"},
                      {"symbol", "€"}, {"precision", 2}, {"balance", "200"}, {"balance_eq", "19000.4"}}}},
                   {"total_eq", "99999"},
                   {"incomplete", false},
                   {"eq_currency_uuid", "c-RUB"}});
    json r = e.ok("balance", {{"wallet", "rev"}, {"date", "2026-09-30"}});
    CHECK(contains(e.reports.last("/api/reports/balance"), "date=2026-10-01"));
    CHECK_EQ(r["date"], "2026-09-30");
    CHECK_EQ(r["wallets"], (json{{{"name", "Revolut"}, {"currency", "EUR"}, {"balance", "200.00"}}}));
    CHECK_EQ(r["total"], "19000.40");
    CHECK_EQ(r["incomplete"], false);

    CHECK(contains(e.error("balance", {{"date", "2026-04-31"}}), "Дата date"));
}

TEST(tools_search_defaults_to_expense) {
    Env e;
    e.reports.set("/api/reports/operations",
                  {{"operations",
                    {{{"operation_uuid", "o1"}, {"date", "2026-09-28T12:05:00"}, {"kind", "expense"},
                      {"amount", "450"}, {"currency_uuid", "c-RUB"}, {"symbol", "₽"}, {"precision", 2},
                      {"amount_eq", "450"}, {"comment", "Такси домой"}, {"category_uuid", "c-ride"},
                      {"category_name", "Проезд"}, {"icon_sha256", "x"}, {"wallet_uuid", "w-cash"},
                      {"wallet_name", "Наличные"}, {"from_uuid", "w-cash"}, {"from_name", "Наличные"},
                      {"to_uuid", "c-ride"}, {"to_name", "Проезд"}}}},
                   {"incomplete", false},
                   {"has_more", true},
                   {"next_cursor_date", "2026-09-28 12:05:00"},
                   {"next_cursor_uuid", "0f8c2a1e-7b3d-4c5a-9e6f-1a2b3c4d5e6f"},
                   {"eq_currency_uuid", "c-RUB"},
                   {"total_eq", "1500"},
                   {"total_incomplete", false},
                   {"total_count", 70}});
    json r = e.ok("search_operations", with(kSeptember, {{"text", json::array({"такси"})}}));
    std::string q = e.reports.last("/api/reports/operations");
    CHECK(contains(q, "kind=expense"));
    CHECK(contains(q, "limit=20"));
    CHECK(contains(q, "q=%D1%82%D0%B0%D0%BA%D1%81%D0%B8"));
    CHECK_EQ(r["kind"], "expense");
    CHECK_EQ(r["has_more"], true);
    CHECK_EQ(r["next_cursor"], "2026-09-28 12:05:00|0f8c2a1e-7b3d-4c5a-9e6f-1a2b3c4d5e6f");
    CHECK_EQ(r["total"], "1500.00");
    CHECK_EQ(r["count"], 70);
    CHECK_EQ(r["operations"][0], (json{{"date", "2026-09-28 12:05"}, {"kind", "expense"}, {"amount", "450.00"},
                                       {"currency", "RUB"}, {"category", "Проезд"}, {"wallet", "Наличные"},
                                       {"comment", "Такси домой"}}));
}

TEST(tools_search_any_without_filters_has_no_total) {
    Env e;
    e.reports.set("/api/reports/operations",
                  {{"operations", json::array()}, {"incomplete", false}, {"has_more", false}, {"eq_currency_uuid", "c-RUB"}});
    json r = e.ok("search_operations", with(kSeptember, {{"kind", "any"}, {"limit", 5}}));
    std::string q = e.reports.last("/api/reports/operations");
    CHECK(!contains(q, "kind="));
    CHECK(contains(q, "limit=5"));
    CHECK_EQ(r["kind"], "any");
    CHECK(!r.contains("total"));
}

TEST(tools_search_any_with_text_is_balance) {
    Env e;
    e.reports.set("/api/reports/operations",
                  {{"operations", json::array()}, {"incomplete", false}, {"has_more", false}, {"eq_currency_uuid", "c-RUB"},
                   {"total_eq", "2000"}, {"total_incomplete", false}, {"total_count", 2}});
    json r = e.ok("search_operations", with(kSeptember, {{"kind", "any"}, {"text", json::array({"Иван"})}}));
    CHECK(!contains(e.reports.last("/api/reports/operations"), "kind="));
    CHECK_EQ(r["total"], "2000.00");
    CHECK_EQ(r["kind"], "any");
}

TEST(tools_search_filters_by_names_and_transfer_row) {
    Env e;
    std::string long_comment;
    for (int i = 0; i < 80; ++i) long_comment += "ж"; // 160 байт
    e.reports.set("/api/reports/operations",
                  {{"operations",
                    {{{"operation_uuid", "o2"}, {"date", "2026-09-10 09:00:00"}, {"kind", "transfer"},
                      {"amount", "100"}, {"currency_uuid", "c-RUB"}, {"symbol", "₽"}, {"precision", 2},
                      {"amount_eq", "100"}, {"comment", long_comment}, {"category_uuid", ""}, {"category_name", ""},
                      {"icon_sha256", ""}, {"wallet_uuid", ""}, {"wallet_name", ""}, {"from_uuid", "w-cash"},
                      {"from_name", "Наличные"}, {"to_uuid", "w-rev"}, {"to_name", "Revolut"}}}},
                   {"incomplete", false}, {"has_more", false}, {"eq_currency_uuid", "c-RUB"},
                   {"total_eq", "100"}, {"total_incomplete", true}, {"total_count", 1}});
    json r = e.ok("search_operations",
                  with(kSeptember, {{"kind", "transfer"}, {"wallets", {"нал", "Revolut"}}, {"categories", {"Проезд"}}}));
    std::string q = e.reports.last("/api/reports/operations");
    CHECK(contains(q, "wallet_uuid=w-cash%2Cw-rev"));
    CHECK(contains(q, "category_uuid=c-ride"));
    CHECK(contains(q, "kind=transfer"));
    const json& op = r["operations"][0];
    CHECK_EQ(op["from"], "Наличные");
    CHECK_EQ(op["to"], "Revolut");
    CHECK(!op.contains("category"));
    CHECK(op["comment"].get<std::string>().size() <= 120u);
    CHECK_EQ(r["incomplete"], true);
}

TEST(tools_search_without_total_from_report_is_unavailable) {
    Env e;
    // wallet-report без фильтров ленты молча игнорирует их и не отдаёт итог.
    e.reports.set("/api/reports/operations",
                  {{"operations", json::array()}, {"incomplete", false}, {"has_more", false}, {"eq_currency_uuid", "c-RUB"}});
    tools::Outcome o = e.call("search_operations", with(kSeptember, {{"text", json::array({"такси"})}}));
    CHECK(o.kind == tools::Outcome::Kind::Unavailable);
    CHECK_EQ(o.text, tools::kReportsUnavailable);
}

TEST(tools_reports_down_is_unavailable) {
    Env e;
    e.reports.set("/api/reports/balance", {{"error", "boom"}}, 500);
    tools::Outcome o = e.call("list_wallets", json::object());
    CHECK(o.kind == tools::Outcome::Kind::Unavailable);
    CHECK_EQ(o.text, "Отчёты временно недоступны, попробуйте позже.");
    // Сеть: ответа нет вовсе.
    e.reports.by_path.erase("/api/reports/balance");
    CHECK(e.call("list_wallets", json::object()).kind == tools::Outcome::Kind::Unavailable);
    // Битый ответ (поле не того типа) — тоже недоступно, а не 500.
    e.reports.set("/api/reports/income-expense-by-month", {{"months", {{{"month_start", 5}}}}});
    CHECK(e.call("income_expense_by_month", kSeptember).kind == tools::Outcome::Kind::Unavailable);
}

TEST(tools_invalid_params_throw) {
    Env e;
    CHECK_THROWS(e.call("search_operations", with(kSeptember, {{"kind", "spending"}})));
    CHECK_THROWS(e.call("list_categories", {{"as_user_id", "u-2"}}));
    CHECK(e.reports.requests.empty());
}

TEST(tools_name_with_parenthetical_note) {
    CHECK_EQ(tools::without_parenthetical("Baby Sitter (Няня)"), std::string("Baby Sitter"));
    CHECK_EQ(tools::without_parenthetical("Няня  (babysitter) "), std::string("Няня"));
    CHECK_EQ(tools::without_parenthetical("Няня"), std::string("Няня"));
    CHECK_EQ(tools::without_parenthetical("(Няня)"), std::string("(Няня)")); // только скобки — не пояснение
    CHECK_EQ(tools::without_parenthetical("Кафе (на работе"), std::string("Кафе (на работе"));

    // Модель передала имя со своим переводом в скобках — находится по имени до скобок.
    Env e;
    e.reports.set("/api/reports/expense-structure", structure({}, "0"));
    e.ok("spending_by_category", with(kSeptember, {{"categories", {"Продукты (еда)"}}}));
    CHECK_EQ(e.reports.count("/api/reports/expense-structure"), 1);
    // Неоднозначность без скобок остаётся неоднозначностью.
    std::string err = e.error("spending_by_category", with(kSeptember, {{"categories", {"каф (кофе)"}}}));
    CHECK_EQ(err, "\"каф (кофе)\" подходит к нескольким: Кафе, Кафе на работе. Уточните.");
}

// ---------------------------------------------------------------------------
// change add-category-analytics: exclude_categories, count/avg_per_month,
// spending_by_category_by_month.

namespace {

// Справочник фикстуры плюс категории сценариев spec («Alcohol», «Bar», «Ремонт»).
json accounts_plus(std::vector<std::pair<const char*, const char*>> extra) {
    json acc = fakes::accounts_body();
    for (const auto& [uuid, name] : extra) {
        acc["accounts"].push_back({{"uuid", uuid}, {"name", name}, {"kind", "category"}, {"system_kind", 0},
                                   {"is_archived", false}, {"icon_sha256", ""}, {"currency_uuid", ""},
                                   {"iso_code", ""}, {"symbol", ""}, {"precision", ""}});
    }
    return acc;
}

json counted_row(const char* uuid, const char* name, json amount, int op_count) {
    json r = cat_row(uuid, name, std::move(amount));
    r["op_count"] = op_count;
    return r;
}

std::string add_strings(const std::string& a, const std::string& b) {
    return decimal::to_string(decimal::add(*decimal::parse(a), *decimal::parse(b)));
}

} // namespace

TEST(tools_categories_and_exclude_together_is_error) {
    Env e;
    const std::string msg = "Задайте categories или exclude_categories, не оба.";
    json both = with(kSeptember, {{"categories", {"Кафе"}}, {"exclude_categories", {"Няня"}}});
    CHECK_EQ(e.error("spending_by_category", both), msg);
    CHECK_EQ(e.error("spending_by_category_by_month", both), msg);
    CHECK_EQ(e.error("search_operations", both), msg);
    CHECK(e.reports.requests.empty()); // ни справочника, ни отчётов
}

TEST(tools_spending_exclude_categories) {
    Env e;
    e.reports.set("/api/reports/expense-structure",
                  structure({counted_row("c-nanny", "Няня", "32000.00", 4), counted_row("c-food", "Продукты", "0.10", 2),
                             counted_row("c-cafe", "Кафе", "0.20", 1), counted_row("c-ride", "Проезд", "1999.99", 9)},
                            "34000.29"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"exclude_categories", {"Няня", "Проезд"}}}));
    CHECK_EQ(only(r["rows"], kOldKeys),
             (json{{{"category", "Продукты"}, {"amount", "0.10"}}, {{"category", "Кафе"}, {"amount", "0.20"}}}));
    CHECK_EQ(r["total"], "0.30"); // точная сумма оставшихся, не total_eq
    CHECK_EQ(r["count"], 3);
    CHECK_EQ(r["incomplete"], false);
    CHECK(!contains(e.reports.last("/api/reports/expense-structure"), "category"));
}

TEST(tools_search_exclude_categories) {
    Env e;
    e.reports.set("/api/reports/operations",
                  {{"operations", json::array()}, {"incomplete", false}, {"has_more", false}, {"eq_currency_uuid", "c-RUB"},
                   {"total_eq", "777.70"}, {"total_incomplete", false}, {"total_count", 12}});
    json r = e.ok("search_operations", with(kSeptember, {{"kind", "any"}, {"exclude_categories", {"Няня", "Кафе"}}}));
    std::string q = e.reports.last("/api/reports/operations");
    CHECK(contains(q, "exclude_category_uuid=c-nanny%2Cc-cafe"));
    CHECK(!contains(q, "&category_uuid="));
    CHECK(!contains(q, "kind="));
    // Исключение — фильтр: итог есть и при kind "any".
    CHECK_EQ(r["total"], "777.70");
    CHECK_EQ(r["count"], 12);
}

// Несколько вариантов text — повторённый q, по одному на вариант; пустые
// и повторы не уходят; итог — из ответа wallet-report (SQL по объединению).
TEST(tools_search_text_variants_as_repeated_q) {
    Env e;
    e.reports.set("/api/reports/operations",
                  {{"operations", json::array()}, {"incomplete", false}, {"has_more", false}, {"eq_currency_uuid", "c-RUB"},
                   {"total_eq", "57"}, {"total_incomplete", false}, {"total_count", 4}});
    json r = e.ok("search_operations",
                  with(kSeptember, {{"text", json::array({"стриж", " barber ", "", "стриж", "haircut"})}}));
    std::string q = e.reports.last("/api/reports/operations");
    CHECK(contains(q, "&q=%D1%81%D1%82%D1%80%D0%B8%D0%B6&q=barber&q=haircut"));
    CHECK_EQ(q.find("&q=", q.find("haircut")), std::string::npos); // ровно три q
    CHECK_EQ(r["total"], "57.00");
    CHECK_EQ(r["count"], 4);

    // Только пустые варианты — фильтра по тексту нет (как text не задан).
    e.ok("search_operations", with(kSeptember, {{"text", json::array({" ", ""})}}));
    CHECK(!contains(e.reports.last("/api/reports/operations"), "q="));
}

// has_more — next_cursor «дата|uuid»; cursor уходит в wallet-report парой
// cursor_date/cursor_uuid вместе с теми же фильтрами; итог тот же.
TEST(tools_search_cursor_pages) {
    Env e;
    const std::string u = "0f8c2a1e-7b3d-4c5a-9e6f-1a2b3c4d5e6f";
    e.reports.push("/api/reports/operations",
                   {{"operations", json::array()}, {"incomplete", false}, {"has_more", true},
                    {"next_cursor_date", "2026-09-20 18:30:00"}, {"next_cursor_uuid", u},
                    {"eq_currency_uuid", "c-RUB"}, {"total_eq", "900"}, {"total_incomplete", false}, {"total_count", 30}});
    e.reports.push("/api/reports/operations",
                   {{"operations", json::array()}, {"incomplete", false}, {"has_more", false},
                    {"eq_currency_uuid", "c-RUB"}, {"total_eq", "900"}, {"total_incomplete", false}, {"total_count", 30}});
    json first = e.ok("search_operations", with(kSeptember, {{"limit", 20}}));
    CHECK(!contains(e.reports.last("/api/reports/operations"), "cursor_"));
    CHECK_EQ(first["has_more"], true);
    CHECK_EQ(first["next_cursor"], "2026-09-20 18:30:00|" + u);

    json second = e.ok("search_operations", with(kSeptember, {{"limit", 20}, {"cursor", first["next_cursor"]}}));
    std::string q = e.reports.last("/api/reports/operations");
    CHECK(contains(q, "cursor_date=2026-09-20%2018%3A30%3A00"));
    CHECK(contains(q, "cursor_uuid=" + u));
    CHECK(contains(q, "kind=expense"));
    CHECK_EQ(second["has_more"], false);
    CHECK(!second.contains("next_cursor"));
    CHECK_EQ(second["total"], first["total"]);
    CHECK_EQ(second["count"], 30);
}

// Битый курсор — ошибка инструмента с подсказкой, до wallet-report.
TEST(tools_search_bad_cursor_is_tool_error) {
    Env e;
    const std::string u = "0f8c2a1e-7b3d-4c5a-9e6f-1a2b3c4d5e6f";
    const std::vector<std::string> bads = {"abc", "2026-09-20 18:30:00", "2026-09-20 18:30:00|xyz",
                                           "2026-13-01 00:00:00|" + u, "2026-09-20;DROP|" + u, "|" + u};
    for (const std::string& bad : bads) {
        tools::Outcome o = e.call("search_operations", with(kSeptember, {{"cursor", bad}}));
        CHECK(o.kind == tools::Outcome::Kind::ToolError);
        CHECK_EQ(o.text, std::string("cursor неверен: передайте next_cursor из прошлого ответа без изменений."));
    }
    CHECK_EQ(e.reports.count("/api/reports/operations"), 0);
}

// has_more без курсора от wallet-report — ответ не по контракту.
TEST(tools_search_has_more_without_cursor_is_unavailable) {
    Env e;
    e.reports.set("/api/reports/operations",
                  {{"operations", json::array()}, {"incomplete", false}, {"has_more", true}, {"eq_currency_uuid", "c-RUB"}});
    tools::Outcome o = e.call("search_operations", with(kSeptember, {{"kind", "any"}}));
    CHECK(o.kind == tools::Outcome::Kind::Unavailable);
}

TEST(tools_spending_count_and_average) {
    Env e;
    // «Лето против девяти месяцев» из spec: 2026-06..08 против 2025-09..2026-05.
    e.reports.push("/api/reports/expense-structure",
                   structure({counted_row("c-nanny", "Няня", "5733.00", 6), counted_row("c-food", "Продукты", "100", 3)},
                             "5833.00"));
    e.reports.push("/api/reports/expense-structure",
                   structure({counted_row("c-nanny", "Няня", "27850.00", 18)}, "27850.00"));
    json r = e.ok("spending_by_category", {{"from", "2026-06-01"},
                                           {"to", "2026-08-31"},
                                           {"compare_from", "2025-09-01"},
                                           {"compare_to", "2026-05-31"},
                                           {"categories", {"Няня"}}});
    CHECK_EQ(r["months"], "3.00");
    CHECK_EQ(r["months_before"], "9.00");
    CHECK_EQ(r["avg_per_month"], "1911.00");
    CHECK_EQ(r["avg_per_month_before"], "3094.44");
    CHECK_EQ(r["count"], 6);
    CHECK_EQ(r["count_before"], 18);
    CHECK_EQ(r["rows"], (json{{{"category", "Няня"}, {"amount", "5733.00"}, {"amount_before", "27850.00"},
                               {"delta", "-22117.00"}, {"count", 6}, {"avg_per_month", "1911.00"},
                               {"count_before", 18}, {"avg_per_month_before", "3094.44"}}}));
    // Прежние ключи — в прежнем порядке, новые — после них.
    std::string text = e.call("spending_by_category", {{"from", "2026-06-01"}, {"to", "2026-08-31"}}).text;
    std::vector<std::string> keys;
    const nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(text);
    for (const auto& item : parsed.items()) keys.push_back(item.key());
    CHECK((keys == std::vector<std::string>{"period", "currency", "rows", "total", "incomplete", "count", "months",
                                            "avg_per_month"}));
}

TEST(tools_spending_all_categories_average_from_sql_total) {
    Env e;
    e.reports.set("/api/reports/expense-structure",
                  structure({counted_row("c-nanny", "Няня", "100", 2), counted_row("c-food", "Продукты", "50", 5)},
                            "150.00"));
    json r = e.ok("spending_by_category", {{"from", "2026-09-01"}, {"to", "2026-09-15"}});
    CHECK_EQ(r["months"], "0.50");
    CHECK_EQ(r["total"], "150.00");
    CHECK_EQ(r["avg_per_month"], "300.00");
    CHECK_EQ(r["count"], 7);
    CHECK_EQ(r["rows"][1], (json{{"category", "Продукты"}, {"amount", "50.00"}, {"count", 5}, {"avg_per_month", "100.00"}}));
}

TEST(tools_spending_without_op_count_from_old_report) {
    Env e;
    // wallet-report до add-category-month-analytics: op_count нет.
    e.reports.set("/api/reports/expense-structure",
                  structure({cat_row("c-nanny", "Няня", "300.00"), cat_row("c-food", "Продукты", "30")}, "330.00"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"categories", {"Няня", "Кафе"}}}));
    CHECK(r["rows"][0]["count"].is_null());
    CHECK(r["rows"][1]["count"].is_null());
    CHECK(r["count"].is_null());
    CHECK_EQ(r["rows"][0]["avg_per_month"], "300.00");
    CHECK_EQ(r["total"], "300.00");
    CHECK_EQ(r["months"], "1.00");
}

TEST(tools_spending_category_without_expense) {
    Env e;
    e.reports.set("/api/reports/expense-structure", structure({counted_row("c-nanny", "Няня", "300.00", 2)}, "300.00"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"categories", {"Кафе"}}}));
    CHECK_EQ(r["rows"][0], (json{{"category", "Кафе"}, {"amount", "0.00"}, {"count", 0}, {"avg_per_month", "0.00"}}));
    CHECK_EQ(r["count"], 0);
}

TEST(tools_by_month_period_limit) {
    Env e;
    std::string err = e.error("spending_by_category_by_month", {{"from", "2022-01-01"}, {"to", "2025-12-31"}});
    CHECK_EQ(err, "Период — не длиннее 36 месяцев. Разбейте его на части.");
    // Ровно 36 задетых месяцев — можно (со сдвигом внутри месяцев).
    e.reports.set("/api/reports/expense-by-category-by-month", fakes::by_month_body({}, {}, "0"));
    e.ok("spending_by_category_by_month", {{"from", "2023-11-15"}, {"to", "2026-10-10"}});
    err = e.error("spending_by_category_by_month", {{"from", "2023-11-15"}, {"to", "2026-11-01"}});
    CHECK_EQ(err, "Период — не длиннее 36 месяцев. Разбейте его на части.");
    CHECK_EQ(e.reports.count("/api/reports/expense-by-category-by-month"), 1);
}

TEST(tools_by_month_no_selected_months) {
    Env e;
    std::string err = e.error("spending_by_category_by_month",
                              {{"from", "2026-09-01"}, {"to", "2026-10-31"}, {"months_of_year", {6, 7, 8}}});
    CHECK_EQ(err, "В периоде нет выбранных месяцев.");
    CHECK(e.reports.requests.empty());
    // Ошибки схемы — до вызова, исключением InvalidParams (-32602).
    CHECK_THROWS(e.call("spending_by_category_by_month", with(kSeptember, {{"months_of_year", {13}}})));
    CHECK_THROWS(e.call("spending_by_category_by_month", with(kSeptember, {{"months_of_year", {9, 9}}})));
    CHECK(e.reports.requests.empty());
}

TEST(tools_by_month_alcohol_two_years) {
    Env e;
    e.reports.set("/api/reports/accounts", accounts_plus({{"c-alc", "Alcohol"}}));
    e.reports.set("/api/reports/expense-by-category-by-month",
                  fakes::by_month_body({fakes::matrix_row("c-alc", "Alcohol", "1304.09", 37,
                                                          {fakes::month_cell("2024-11-01", "100.00", 3),
                                                           fakes::month_cell("2025-07-01", "1000.09", 30),
                                                           fakes::month_cell("2026-11-01", "204.00", 4)}),
                                        fakes::matrix_row("c-food", "Продукты", "5000", 50,
                                                          {fakes::month_cell("2025-01-01", "5000", 50)})},
                                       {"2024-11-01", "2026-11-01"}, "6304.09"));
    json r = e.ok("spending_by_category_by_month",
                  {{"from", "2024-11-05"}, {"to", "2026-11-04"}, {"categories", {"Alcohol"}}});
    std::string q = e.reports.last("/api/reports/expense-by-category-by-month");
    CHECK(contains(q, "from=2024-11-05%2000%3A00%3A00"));
    CHECK(contains(q, "to=2026-11-04%2023%3A59%3A59.999999"));
    CHECK(contains(q, "eq_currency=RUB"));
    CHECK(!contains(q, "category_uuid")); // фильтры категорий — на стороне wallet-mcp (D1)
    CHECK_EQ(r["rows"], (json{{{"category", "Alcohol"},
                               {"amount", "1304.09"},
                               {"count", 37},
                               {"avg_per_month", "54.34"},
                               {"by_month", {{"2024-11", "100.00"}, {"2025-07", "1000.09"}, {"2026-11", "204.00"}}}}}));
    CHECK_EQ(r["months"], "24.00");
    CHECK_EQ(r["total"], "1304.09");
    CHECK_EQ(r["count"], 37);
    CHECK_EQ(r["avg_per_month"], "54.34");
    CHECK_EQ(r["period"], (json{{"from", "2024-11-05"}, {"to", "2026-11-04"}}));
    CHECK_EQ(r["currency"], "RUB");
    CHECK_EQ(r["incomplete"], false);
    CHECK(!r.contains("months_of_year"));
}

TEST(tools_by_month_by_month_only_with_categories) {
    Env e;
    e.reports.set("/api/reports/accounts", accounts_plus({{"c-bar", "Bar"}}));
    e.reports.set("/api/reports/expense-by-category-by-month",
                  fakes::by_month_body({fakes::matrix_row("c-bar", "Bar", "90", 3,
                                                          {fakes::month_cell("2026-06-01", "40", 2),
                                                           fakes::month_cell("2026-08-01", "50", 1)}),
                                        fakes::matrix_row("c-food", "Продукты", "300", 6,
                                                          {fakes::month_cell("2026-06-01", "100", 2),
                                                           fakes::month_cell("2026-07-01", "100", 2),
                                                           fakes::month_cell("2026-08-01", "100", 2)})},
                                       {"2026-06-01", "2026-07-01", "2026-08-01"}, "390.00"));
    const json summer = {{"from", "2026-06-01"}, {"to", "2026-08-31"}};
    // Без фильтров — все категории без by_month, total — из total_eq.
    json all = e.ok("spending_by_category_by_month", summer);
    CHECK_EQ(all["rows"].size(), 2u);
    for (const auto& row : all["rows"]) CHECK(!row.contains("by_month"));
    CHECK_EQ(all["total"], "390.00");
    CHECK_EQ(all["count"], 9);
    CHECK_EQ(all["months"], "3.00");
    CHECK_EQ(all["avg_per_month"], "130.00");
    // С categories — by_month только с непустыми месяцами.
    json bar = e.ok("spending_by_category_by_month", with(summer, {{"categories", {"Bar"}}}));
    CHECK_EQ(bar["rows"][0]["by_month"], (json{{"2026-06", "40.00"}, {"2026-08", "50.00"}}));
    // Выбранная категория без трат — нули и пустой by_month.
    json cafe = e.ok("spending_by_category_by_month", with(summer, {{"categories", {"Кафе"}}}));
    CHECK_EQ(cafe["rows"][0], (json{{"category", "Кафе"}, {"amount", "0.00"}, {"count", 0},
                                    {"avg_per_month", "0.00"}, {"by_month", json::object()}}));
}

TEST(tools_by_month_summer_plus_rest_equals_whole) {
    Env e;
    // 2024-09-01..2026-08-31: «Продукты» каждый месяц по 10.01, «Няня» — только летом 2025 и 2026.
    std::vector<json> food_cells, nanny_cells;
    std::vector<std::string> all_months;
    for (int i = 0; i < 24; ++i) {
        int y = 2024 + (8 + i) / 12;
        int m = (8 + i) % 12 + 1;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-01", y, m);
        all_months.push_back(buf);
    }
    for (const auto& m : all_months) {
        food_cells.push_back(fakes::month_cell(m.c_str(), "10.01", 1));
        int month = std::stoi(m.substr(5, 2));
        if (month >= 6 && month <= 8) nanny_cells.push_back(fakes::month_cell(m.c_str(), "1000.50", 2));
    }
    e.reports.set("/api/reports/expense-by-category-by-month",
                  fakes::by_month_body({fakes::matrix_row("c-nanny", "Няня", "6003.00", 12, nanny_cells),
                                        fakes::matrix_row("c-food", "Продукты", "240.24", 24, food_cells)},
                                       all_months, "6243.24"));
    const json period = {{"from", "2024-09-01"}, {"to", "2026-08-31"}};
    json whole = e.ok("spending_by_category_by_month", period);
    json summer = e.ok("spending_by_category_by_month", with(period, {{"months_of_year", {8, 6, 7}}}));
    json rest = e.ok("spending_by_category_by_month", with(period, {{"months_of_year", {1, 2, 3, 4, 5, 9, 10, 11, 12}}}));
    CHECK_EQ(whole["months"], "24.00");
    CHECK_EQ(summer["months"], "6.00");
    CHECK_EQ(rest["months"], "18.00");
    CHECK_EQ(summer["months_of_year"], (json{6, 7, 8})); // эхо по возрастанию
    CHECK_EQ(summer["total"], "6063.06");
    CHECK_EQ(summer["avg_per_month"], "1010.51");
    CHECK_EQ(summer["count"], 18);
    CHECK_EQ(rest["total"], "180.18");
    CHECK_EQ(rest["avg_per_month"], "10.01");
    // «Няня» летом не тратилась в остальные месяцы — строки нет.
    CHECK_EQ(rest["rows"].size(), 1u);
    CHECK_EQ(rest["rows"][0]["category"], "Продукты");
    CHECK_EQ(add_strings(summer["total"], rest["total"]), whole["total"].get<std::string>());
    CHECK_EQ(summer["count"].get<int>() + rest["count"].get<int>(), whole["count"].get<int>());
    CHECK_EQ(e.reports.count("/api/reports/expense-by-category-by-month"), 3);
}

TEST(tools_by_month_exclude) {
    Env e;
    e.reports.set("/api/reports/expense-by-category-by-month",
                  fakes::by_month_body({fakes::matrix_row("c-nanny", "Няня", "32000.00", 4,
                                                          {fakes::month_cell("2026-09-01", "32000.00", 4)}),
                                        fakes::matrix_row("c-food", "Продукты", "0.10", 1,
                                                          {fakes::month_cell("2026-09-01", "0.10", 1)}),
                                        fakes::matrix_row("c-cafe", "Кафе", "0.20", 2,
                                                          {fakes::month_cell("2026-09-01", "0.20", 2)}),
                                        fakes::matrix_row("c-ride", "Проезд", nullptr, 1,
                                                          {fakes::month_cell("2026-09-01", nullptr, 1)})},
                                       {"2026-09-01"}, "32000.30", true));
    json r = e.ok("spending_by_category_by_month", with(kSeptember, {{"exclude_categories", {"Няня"}}}));
    CHECK_EQ(r["rows"].size(), 3u);
    for (const auto& row : r["rows"]) CHECK(!row.contains("by_month"));
    CHECK_EQ(r["total"], "0.30");
    CHECK_EQ(r["count"], 4);
    CHECK(r["rows"][2]["amount"].is_null()); // курса не нашлось
    CHECK(r["rows"][2]["avg_per_month"].is_null());
    CHECK_EQ(r["incomplete"], true);
}

TEST(tools_by_month_report_without_endpoint_is_unavailable) {
    Env e;
    // wallet-report без add-category-month-analytics: 404.
    e.reports.set("/api/reports/expense-by-category-by-month", {{"error", "not found"}}, 404);
    tools::Outcome o = e.call("spending_by_category_by_month", kSeptember);
    CHECK(o.kind == tools::Outcome::Kind::Unavailable);
    CHECK_EQ(o.text, tools::kReportsUnavailable);
}

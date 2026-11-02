// Инструменты на фейковом wallet-report (fakes::FakeReports).
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
    CHECK_EQ(r["rows"], (json{{{"category", "Няня"}, {"amount", "32000.00"}},
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
    CHECK_EQ(r["rows"][1], (json{{"category", "Продукты"}, {"amount", "0.00"}}));
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
    CHECK_EQ(r["rows"], (json{{{"category", "Продукты"}, {"amount", "12000.50"}, {"amount_before", "10000.25"},
                               {"delta", "2000.25"}}}));
    CHECK_EQ(r["total"], "12000.50");
    CHECK_EQ(r["total_before"], "10000.25");
}

TEST(tools_spending_compare_all_categories) {
    Env e;
    e.reports.push("/api/reports/expense-structure", structure({cat_row("c-food", "Продукты", "100")}, "100"));
    e.reports.push("/api/reports/expense-structure", structure({cat_row("c-cafe", "Кафе", "40")}, "40"));
    json r = e.ok("spending_by_category", with(kSeptember, {{"compare_from", "2026-08-01"}, {"compare_to", "2026-08-31"}}));
    CHECK_EQ(r["rows"], (json{{{"category", "Продукты"}, {"amount", "100.00"}, {"amount_before", "0.00"}, {"delta", "100.00"}},
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
                   {"eq_currency_uuid", "c-RUB"},
                   {"total_eq", "1500"},
                   {"total_incomplete", false},
                   {"total_count", 70}});
    json r = e.ok("search_operations", with(kSeptember, {{"text", "такси"}}));
    std::string q = e.reports.last("/api/reports/operations");
    CHECK(contains(q, "kind=expense"));
    CHECK(contains(q, "limit=20"));
    CHECK(contains(q, "q=%D1%82%D0%B0%D0%BA%D1%81%D0%B8"));
    CHECK_EQ(r["kind"], "expense");
    CHECK_EQ(r["has_more"], true);
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
    json r = e.ok("search_operations", with(kSeptember, {{"kind", "any"}, {"text", "Иван"}}));
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
    tools::Outcome o = e.call("search_operations", with(kSeptember, {{"text", "такси"}}));
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

// Фейковые google-auth и wallet-report для юнит-тестов: ответы по префиксу
// пути, журнал запросов. Без сети.
#pragma once

#include <chrono>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "access.hpp"
#include "auth_client.hpp"
#include "clients/reports_client.hpp"
#include "nlohmann/json.hpp"

namespace fakes {

// Токен правильного формата: wmcp_ + 43 символа base64url.
inline std::string token(char fill = 'A') { return "wmcp_" + std::string(43, fill); }

// Оба обмена берут исход из next; calls считает оба, jwt_calls — только
// /internal/token/exchange.
struct FakeAuth : auth::AuthApi {
    auth::Exchange next;
    int calls = 0;
    int jwt_calls = 0;
    std::vector<std::string> tokens;
    std::vector<std::string> audiences; // audience каждого обмена ("" — не задан)
    int counter = 0; // номер выданного JWT — видно, что обмен был новым

    FakeAuth() { ok(); }
    void ok(int expires_in = 300) {
        next = {};
        next.result = auth::Exchange::Result::Ok;
        next.expires_in = expires_in;
        next.user_id = "11111111-1111-4111-8111-111111111111";
    }
    void invalid() {
        next = {};
        next.result = auth::Exchange::Result::Invalid;
    }
    void unavailable() {
        next = {};
        next.result = auth::Exchange::Result::Unavailable;
        next.error = "fake down";
    }
    // 401 invalid_client на /internal/token/exchange (неверный секрет).
    void invalid_client() {
        next = {};
        next.result = auth::Exchange::Result::Unavailable;
        next.invalid_client = true;
        next.error = "invalid_client";
    }
    auth::Exchange exchange(const std::string& t, const std::string& audience) override {
        ++calls;
        tokens.push_back(t);
        audiences.push_back(audience);
        auth::Exchange e = next;
        e.invalid_client = false; // у /internal/mcp/token такого ответа нет
        if (e.result == auth::Exchange::Result::Ok) e.access_token = "jwt-" + std::to_string(++counter);
        return e;
    }
    auth::Exchange exchange_jwt(const std::string& t, const std::string& audience) override {
        ++calls;
        ++jwt_calls;
        tokens.push_back(t);
        audiences.push_back(audience);
        auth::Exchange e = next;
        e.user_id.clear(); // в ответе RFC 8693 user_id нет
        if (e.result == auth::Exchange::Result::Ok) e.access_token = "jwt-" + std::to_string(++counter);
        return e;
    }
};

struct FakeReports : reports::ReportsApi {
    struct Canned {
        int status = 200;
        nlohmann::json body;
    };
    std::map<std::string, std::vector<Canned>> by_path; // путь без query -> очередь (последний повторяется)
    std::vector<std::string> requests;                 // path_and_query
    std::vector<std::string> jwts;

    void set(const std::string& path, nlohmann::json body, int status = 200) { by_path[path] = {{status, std::move(body)}}; }
    void push(const std::string& path, nlohmann::json body, int status = 200) {
        by_path[path].push_back({status, std::move(body)});
    }

    reports::Response get(const std::string& path_and_query, const std::string& access_token) override {
        requests.push_back(path_and_query);
        jwts.push_back(access_token);
        std::string path = path_and_query.substr(0, path_and_query.find('?'));
        reports::Response r;
        auto it = by_path.find(path);
        if (it == by_path.end() || it->second.empty()) {
            r.status = 0;
            r.error = "fake: нет ответа для " + path;
            return r;
        }
        Canned c = it->second.front();
        if (it->second.size() > 1) it->second.erase(it->second.begin());
        r.status = c.status;
        r.body = c.body;
        return r;
    }

    int count(const std::string& path) const {
        int n = 0;
        for (const auto& r : requests) {
            if (r.substr(0, r.find('?')) == path) ++n;
        }
        return n;
    }
    std::string last(const std::string& path) const {
        for (auto it = requests.rbegin(); it != requests.rend(); ++it) {
            if (it->substr(0, it->find('?')) == path) return *it;
        }
        return {};
    }
};

struct ManualClock {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
    mcpaccess::Clock fn() {
        return [this] { return now; };
    }
    void advance(std::chrono::seconds s) { now += s; }
};

// Перехват stdout/stderr (applog пишет туда без LOG_DIR).
struct CaptureLog {
    std::ostringstream buf;
    std::streambuf* out;
    std::streambuf* err;
    CaptureLog() : out(std::cout.rdbuf(buf.rdbuf())), err(std::cerr.rdbuf(buf.rdbuf())) {}
    ~CaptureLog() {
        std::cout.rdbuf(out);
        std::cerr.rdbuf(err);
    }
    std::string text() const { return buf.str(); }
};

// Справочник: кошельки «Наличные» (RUB), «Revolut» (EUR), категории, в том
// числе системная Transfer.
inline nlohmann::json accounts_body() {
    using nlohmann::json;
    auto wallet = [](const char* uuid, const char* name, const char* iso, const char* precision) {
        return json{{"uuid", uuid},          {"name", name},           {"kind", "wallet"}, {"system_kind", 0},
                    {"is_archived", false},  {"icon_sha256", ""},      {"currency_uuid", std::string("c-") + iso},
                    {"iso_code", iso},       {"symbol", iso},          {"precision", precision}};
    };
    auto category = [](const char* uuid, const char* name, int system_kind = 0) {
        return json{{"uuid", uuid},        {"name", name},     {"kind", "category"}, {"system_kind", system_kind},
                    {"is_archived", false}, {"icon_sha256", ""}, {"currency_uuid", ""}, {"iso_code", ""},
                    {"symbol", ""},         {"precision", ""}};
    };
    return {{"accounts",
             {wallet("w-cash", "Наличные", "RUB", "2"), wallet("w-rev", "Revolut", "EUR", "2"),
              category("c-nanny", "Няня"), category("c-food", "Продукты"), category("c-ride", "Проезд"),
              category("c-cafe", "Кафе"), category("c-cafe-work", "Кафе на работе"),
              category("c-transfer", "Transfer", 1)}}};
}

// Ответ GET /api/reports/expense-by-category-by-month (wallet-report change
// add-category-month-analytics, wallet-report PR #6, reportExpenseByCategoryByMonth):
// categories[{account_uuid, account_name, icon_sha256, amount_eq, op_count,
// months[{month_start, amount_eq, op_count, incomplete}]}], months — строки
// "ГГГГ-ММ-01" всех месяцев периода, total_eq, incomplete, eq_currency_uuid.
inline nlohmann::json month_cell(const char* month_start, nlohmann::json amount_eq, int op_count) {
    return {{"month_start", month_start}, {"amount_eq", std::move(amount_eq)}, {"op_count", op_count},
            {"incomplete", false}};
}

inline nlohmann::json matrix_row(const char* uuid, const char* name, nlohmann::json total_eq, int op_count,
                                 std::vector<nlohmann::json> months) {
    return {{"account_uuid", uuid},       {"account_name", name}, {"icon_sha256", "x"},
            {"amount_eq", std::move(total_eq)}, {"op_count", op_count}, {"months", std::move(months)}};
}

inline nlohmann::json by_month_body(std::vector<nlohmann::json> categories, std::vector<std::string> period_months,
                                    const char* total_eq, bool incomplete = false) {
    nlohmann::json months = nlohmann::json::array();
    for (const auto& m : period_months) months.push_back(m);
    return {{"months", months},           {"categories", std::move(categories)}, {"total_eq", total_eq},
            {"incomplete", incomplete},   {"eq_currency_uuid", "c-RUB"}};
}

inline nlohmann::json currencies_body() {
    using nlohmann::json;
    auto cur = [](const char* iso, int precision, bool show) {
        return json{{"uuid", std::string("c-") + iso}, {"iso_code", iso}, {"name", iso}, {"symbol", iso},
                    {"icon_sha256", ""}, {"show_in_equivalent", show}, {"is_own", false}, {"icon_uuid", ""},
                    {"precision", precision}};
    };
    return {{"currencies", {cur("EUR", 2, true), cur("JPY", 0, true), cur("RUB", 2, true), cur("XAU", 4, false)}}};
}

} // namespace fakes

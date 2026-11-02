#include "access.hpp"
#include "fakes.hpp"
#include "test_framework.hpp"
#include "util/metrics.hpp"

using std::chrono::seconds;

namespace {

int metric_value(const std::string& series) {
    std::string text = metrics::render();
    auto pos = text.find(series + " ");
    if (pos == std::string::npos) return 0;
    return std::stoi(text.substr(pos + series.size() + 1));
}

} // namespace

TEST(access_token_format) {
    CHECK(mcpaccess::looks_like_mcp_token(fakes::token()));
    CHECK(mcpaccess::looks_like_mcp_token("wmcp_" + std::string(21, '-') + std::string(22, '_')));
    CHECK(!mcpaccess::looks_like_mcp_token("wmcp_" + std::string(42, 'A')));
    CHECK(!mcpaccess::looks_like_mcp_token("wmcp_" + std::string(44, 'A')));
    CHECK(!mcpaccess::looks_like_mcp_token("wmcp_" + std::string(42, 'A') + "="));
    CHECK(!mcpaccess::looks_like_mcp_token("eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiJ1In0.c2ln")); // JWT веб-сессии
    CHECK(!mcpaccess::looks_like_mcp_token("WMCP_" + std::string(43, 'A')));
}

TEST(access_bearer_header) {
    CHECK_EQ(mcpaccess::bearer_token("Bearer abc"), "abc");
    CHECK_EQ(mcpaccess::bearer_token("bearer   abc  "), "abc");
    CHECK_EQ(mcpaccess::bearer_token("Basic abc"), "");
    CHECK_EQ(mcpaccess::bearer_token("Bearer"), "");
    CHECK_EQ(mcpaccess::bearer_token(""), "");
}

TEST(access_key_is_sha256_hex_not_token) {
    std::string key = mcpaccess::token_key(fakes::token());
    CHECK_EQ(key.size(), 64u);
    CHECK(key.find("wmcp_") == std::string::npos);
    CHECK(key != mcpaccess::token_key(fakes::token('B')));
}

TEST(access_cache_hit_after_miss) {
    fakes::FakeAuth auth;
    fakes::ManualClock clock;
    mcpaccess::TokenCache cache(auth, clock.fn());
    int hits = metric_value("mcp_token_cache_total{result=\"hit\"}");
    int misses = metric_value("mcp_token_cache_total{result=\"miss\"}");
    int ok = metric_value("mcp_token_exchange_total{result=\"ok\"}");

    // Пять вызовов за минуту одним токеном — один обмен.
    for (int i = 0; i < 5; ++i) {
        mcpaccess::Session s = cache.get(fakes::token());
        CHECK(s.result == auth::Exchange::Result::Ok);
        CHECK_EQ(s.jwt, "jwt-1");
        clock.advance(seconds(12));
    }
    CHECK_EQ(auth.calls, 1);
    CHECK_EQ(metric_value("mcp_token_cache_total{result=\"hit\"}") - hits, 4);
    CHECK_EQ(metric_value("mcp_token_cache_total{result=\"miss\"}") - misses, 1);
    CHECK_EQ(metric_value("mcp_token_exchange_total{result=\"ok\"}") - ok, 1);

    // Другой токен — свой обмен.
    CHECK_EQ(cache.get(fakes::token('B')).jwt, "jwt-2");
    CHECK_EQ(auth.calls, 2);
}

TEST(access_cache_expires_30s_before_exp) {
    fakes::FakeAuth auth;
    fakes::ManualClock clock;
    mcpaccess::TokenCache cache(auth, clock.fn());
    cache.get(fakes::token());
    clock.advance(seconds(269)); // до exp 31 с — ещё годен
    CHECK_EQ(cache.get(fakes::token()).jwt, "jwt-1");
    clock.advance(seconds(11)); // до exp 20 с — меняем заново
    CHECK_EQ(cache.get(fakes::token()).jwt, "jwt-2");
    CHECK_EQ(auth.calls, 2);
}

TEST(access_cache_does_not_keep_failures) {
    fakes::FakeAuth auth;
    mcpaccess::TokenCache cache(auth);
    auth.invalid();
    CHECK(cache.get(fakes::token()).result == auth::Exchange::Result::Invalid);
    auth.unavailable();
    CHECK(cache.get(fakes::token()).result == auth::Exchange::Result::Unavailable);
    // google-auth поднялся — следующий запрос проходит.
    auth.ok();
    CHECK(cache.get(fakes::token()).result == auth::Exchange::Result::Ok);
    CHECK_EQ(auth.calls, 3);
    CHECK_EQ(cache.size(), 1u);
}

TEST(access_short_ttl_not_cached) {
    fakes::FakeAuth auth;
    mcpaccess::TokenCache cache(auth);
    auth.ok(20); // меньше запаса — не кешируется
    cache.get(fakes::token());
    cache.get(fakes::token());
    CHECK_EQ(auth.calls, 2);
    CHECK_EQ(cache.size(), 0u);
}

TEST(access_report_401_refreshes_once_and_retries) {
    fakes::FakeAuth auth;
    fakes::FakeReports reports;
    mcpaccess::TokenCache cache(auth);
    mcpaccess::Session s = cache.get(fakes::token());
    reports.push("/api/reports/accounts", {{"error", "unauthorized"}}, 401);
    reports.push("/api/reports/accounts", fakes::accounts_body(), 200);
    mcpaccess::UserReports ur(reports, cache, fakes::token(), s);
    reports::Response r = ur.get("/api/reports/accounts");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(auth.calls, 2);
    REQUIRE(reports.jwts.size() == 2u);
    CHECK_EQ(reports.jwts[0], "jwt-1");
    CHECK_EQ(reports.jwts[1], "jwt-2");
    // Новый JWT — в кеше.
    CHECK_EQ(cache.get(fakes::token()).jwt, "jwt-2");
}

TEST(access_report_401_twice_is_rejected) {
    fakes::FakeAuth auth;
    fakes::FakeReports reports;
    mcpaccess::TokenCache cache(auth);
    reports.set("/api/reports/accounts", {{"error", "unauthorized"}}, 401);
    mcpaccess::UserReports ur(reports, cache, fakes::token(), cache.get(fakes::token()));
    CHECK_THROWS(ur.get("/api/reports/accounts"));
    CHECK_EQ(auth.calls, 2);
    CHECK_EQ(reports.requests.size(), 2u);
    // Повтор — один на запрос: следующий вызов того же запроса не меняет токен.
    CHECK_THROWS(ur.get("/api/reports/accounts"));
    CHECK_EQ(auth.calls, 2);
}

TEST(access_report_401_then_revoked_token) {
    fakes::FakeAuth auth;
    fakes::FakeReports reports;
    mcpaccess::TokenCache cache(auth);
    reports.set("/api/reports/accounts", {{"error", "unauthorized"}}, 401);
    mcpaccess::UserReports ur(reports, cache, fakes::token(), cache.get(fakes::token()));
    auth.invalid();
    bool rejected = false;
    try {
        ur.get("/api/reports/accounts");
    } catch (const mcpaccess::AuthRejected&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK_EQ(reports.requests.size(), 1u);

    auth.unavailable();
    mcpaccess::UserReports ur2(reports, cache, fakes::token(), {auth::Exchange::Result::Ok, "", "jwt-x", ""});
    bool unavailable = false;
    try {
        ur2.get("/api/reports/accounts");
    } catch (const mcpaccess::AuthUnavailable&) {
        unavailable = true;
    }
    CHECK(unavailable);
}

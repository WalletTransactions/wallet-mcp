#include "access.hpp"
#include "fakes.hpp"
#include "metrics2.hpp"
#include "test_framework.hpp"
#include "test_keys.hpp"
#include "util/metrics.hpp"

using std::chrono::seconds;

namespace {

int metric_value(const std::string& series) {
    std::string text = metrics::render() + metrics2::render();
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
    int ok = metric_value("mcp_token_exchange_total{result=\"ok\",kind=\"pat\"}");

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
    CHECK_EQ(metric_value("mcp_token_exchange_total{result=\"ok\",kind=\"pat\"}") - ok, 1);

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

// --- Проверка JWT для wallet-mcp (D2) ---

TEST(access_verify_jwt_ok_with_actor) {
    std::string token = testkeys::mcp_jwt();
    mcpaccess::VerifiedJwt v = mcpaccess::verify_jwt(token, testkeys::jwks_lookup());
    CHECK(v.ok);
    CHECK_EQ(v.user_id, testkeys::kSub);
    CHECK_EQ(v.actor, "wallet-ai");
    CHECK(v.exp > std::time(nullptr));
    // aud строкой — тоже по RFC 7519.
    auto claims = testkeys::mcp_claims();
    claims["aud"] = "wallet-mcp";
    CHECK(mcpaccess::verify_jwt(testkeys::google_auth().sign(claims), testkeys::jwks_lookup()).ok);
}

TEST(access_verify_jwt_rejects) {
    auto reason = [](const nlohmann::json& claims) {
        return mcpaccess::verify_jwt(testkeys::google_auth().sign(claims), testkeys::jwks_lookup()).reason;
    };
    auto claims = testkeys::mcp_claims();
    claims["aud"] = nlohmann::json::array({"wallet-report"});
    CHECK_EQ(reason(claims), "jwt_wrong_audience");

    claims = testkeys::mcp_claims();
    claims["scope"] = "reports:read";
    CHECK_EQ(reason(claims), "jwt_insufficient_scope");

    claims = testkeys::mcp_claims();
    claims["iss"] = "evil";
    CHECK_EQ(reason(claims), "jwt_wrong_issuer");

    claims = testkeys::mcp_claims();
    claims.erase("aud");
    CHECK_EQ(reason(claims), "jwt_legacy");

    claims = testkeys::mcp_claims();
    claims.erase("sub");
    CHECK_EQ(reason(claims), "jwt_no_sub");

    CHECK_EQ(reason(testkeys::web_claims()), "jwt_wrong_audience");
    CHECK_EQ(reason(testkeys::mcp_claims(-5)), "jwt_expired");

    claims = testkeys::mcp_claims();
    claims.erase("exp");
    CHECK_EQ(reason(claims), "jwt_expired");
}

TEST(access_verify_jwt_foreign_signature) {
    // Чужой ключ: kid неизвестен JWKS.
    std::string foreign = testkeys::foreign().sign(testkeys::mcp_claims());
    CHECK_EQ(mcpaccess::verify_jwt(foreign, testkeys::jwks_lookup()).reason, "jwt_unknown_key");
    // Чужой ключ под kid google-auth: подпись не сходится.
    mcpaccess::KeyLookup any_kid = [](const std::string&) -> std::optional<jwks::PublicKey> {
        return testkeys::google_auth().public_key();
    };
    CHECK_EQ(mcpaccess::verify_jwt(foreign, any_kid).reason, "jwt_signature");
    // Подменённый payload при подлинной подписи.
    std::string token = testkeys::mcp_jwt();
    std::string other = testkeys::google_auth().sign(testkeys::web_claims());
    std::string spliced = token.substr(0, token.find('.')) + other.substr(other.find('.'), other.rfind('.') - other.find('.')) +
                          token.substr(token.rfind('.'));
    CHECK_EQ(mcpaccess::verify_jwt(spliced, testkeys::jwks_lookup()).reason, "jwt_signature");
}

TEST(access_verify_jwt_garbage_and_disabled) {
    CHECK_EQ(mcpaccess::verify_jwt("not-a-jwt", testkeys::jwks_lookup()).reason, "jwt_malformed");
    CHECK_EQ(mcpaccess::verify_jwt("eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiJ1In0.c2ln", testkeys::jwks_lookup()).reason,
             "jwt_malformed"); // без kid
    CHECK_EQ(mcpaccess::verify_jwt(testkeys::mcp_jwt(), mcpaccess::KeyLookup{}).reason, "jwt_disabled");
}

TEST(access_actor_chain) {
    using nlohmann::json;
    CHECK_EQ(mcpaccess::actor_chain(json{{"sub", "u"}}), "");
    CHECK_EQ(mcpaccess::actor_chain(json{{"act", {{"sub", "wallet-ai"}}}}), "wallet-ai");
    CHECK_EQ(mcpaccess::actor_chain(json{{"act", {{"sub", "wallet-mcp"}, {"act", {{"sub", "wallet-ai"}}}}}}),
             "wallet-mcp,wallet-ai");
    CHECK_EQ(mcpaccess::actor_chain(json{{"act", "wallet-ai"}}), "");
}

// --- Кеш для JWT (D3) ---

namespace {

mcpaccess::Presented jwt_presented(const std::string& token, std::time_t not_after) {
    return {token, mcpaccess::Credential::Jwt, testkeys::kSub, not_after};
}

} // namespace

TEST(access_cache_jwt_series_one_exchange) {
    fakes::FakeAuth auth;
    fakes::ManualClock clock;
    std::time_t wall = 1'800'000'000;
    mcpaccess::TokenCache cache(auth, clock.fn(), [&wall] { return wall; });
    int jwt_ok = metric_value("mcp_token_exchange_total{result=\"ok\",kind=\"jwt\"}");

    auto p = jwt_presented("eyJ.jwt.1", wall + 300);
    for (int i = 0; i < 5; ++i) {
        mcpaccess::Session s = cache.get(p);
        CHECK(s.result == auth::Exchange::Result::Ok);
        CHECK_EQ(s.jwt, "jwt-1");
        CHECK_EQ(s.user_id, testkeys::kSub); // sub входящего JWT, не из ответа обмена
        clock.advance(seconds(12));
        wall += 12;
    }
    CHECK_EQ(auth.calls, 1);
    CHECK_EQ(auth.jwt_calls, 1);
    CHECK_EQ(auth.tokens.back(), "eyJ.jwt.1");
    CHECK_EQ(metric_value("mcp_token_exchange_total{result=\"ok\",kind=\"jwt\"}") - jwt_ok, 1);
}

TEST(access_cache_jwt_does_not_outlive_incoming_exp) {
    fakes::FakeAuth auth; // JWT для wallet-report — на 300 с
    fakes::ManualClock clock;
    std::time_t wall = 1'800'000'000;
    mcpaccess::TokenCache cache(auth, clock.fn(), [&wall] { return wall; });

    // Входящему JWT осталось 100 с: запись живёт до exp − 30 с, то есть 70 с.
    auto p = jwt_presented("eyJ.jwt.2", wall + 100);
    CHECK_EQ(cache.get(p).jwt, "jwt-1");
    clock.advance(seconds(69));
    wall += 69;
    CHECK_EQ(cache.get(p).jwt, "jwt-1");
    clock.advance(seconds(2));
    wall += 2;
    CHECK_EQ(cache.get(p).jwt, "jwt-2");
    CHECK_EQ(auth.jwt_calls, 2);

    // Входящему JWT меньше запаса — не кешируется вовсе.
    auto short_lived = jwt_presented("eyJ.jwt.3", wall + 20);
    cache.get(short_lived);
    cache.get(short_lived);
    CHECK_EQ(auth.jwt_calls, 4);
}

TEST(access_report_401_refresh_works_for_jwt) {
    fakes::FakeAuth auth;
    fakes::FakeReports reports;
    mcpaccess::TokenCache cache(auth);
    auto p = jwt_presented("eyJ.jwt.4", std::time(nullptr) + 300);
    mcpaccess::Session s = cache.get(p);
    reports.push("/api/reports/accounts", {{"error", "unauthorized"}}, 401);
    reports.push("/api/reports/accounts", fakes::accounts_body(), 200);
    mcpaccess::UserReports ur(reports, cache, p, s);
    CHECK_EQ(ur.get("/api/reports/accounts").status, 200);
    CHECK_EQ(auth.jwt_calls, 2);
    CHECK_EQ(auth.calls, 2); // /internal/mcp/token не вызывался
    REQUIRE(reports.jwts.size() == 2u);
    CHECK_EQ(reports.jwts[1], "jwt-2");
}

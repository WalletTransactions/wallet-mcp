#include "access.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

#include "metrics2.hpp"
#include "util/crypto.hpp"
#include "util/jwt.hpp"
#include "util/jwt_claims.hpp"
#include "util/log.hpp"
#include "util/metrics.hpp"

namespace mcpaccess {

const char* const kInvalidTokenMessage =
    "MCP-токен недействителен или истёк — выпустите новый в веб-кабинете: Настройки → Устройства";
const char* const kAuthUnavailableMessage = "Сервис авторизации временно недоступен, попробуйте позже";

namespace {

constexpr const char* kExchangeMetric = "mcp_token_exchange_total";
constexpr const char* kExchangeHelp = "Обмены предъявленного токена на JWT для wallet-report в google-auth по исходу и виду.";
constexpr const char* kCacheMetric = "mcp_token_cache_total";
constexpr const char* kCacheHelp = "Обращения к кешу JWT по предъявленному токену: hit/miss.";
constexpr const char* kDocsExchangeMetric = "mcp_docs_token_exchange_total";
constexpr const char* kDocsExchangeHelp = "Обмены предъявленного токена на JWT для wallet-docs в google-auth по исходу и виду.";
constexpr const char* kDocsCacheMetric = "mcp_docs_token_cache_total";
constexpr const char* kDocsCacheHelp = "Обращения к кешу JWT для wallet-docs: hit/miss.";
constexpr unsigned kSweepEvery = 100;
// Глубже цепочки act в стеке не бывает (wallet-ai → wallet-mcp); предел —
// защита от вложенного мусора, а не политика.
constexpr int kMaxActorDepth = 8;

std::string to_hex(const std::vector<unsigned char>& bytes) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

VerifiedJwt rejected(std::string reason) {
    VerifiedJwt v;
    v.reason = std::move(reason);
    return v;
}

} // namespace

Clock steady_clock() {
    return [] { return std::chrono::steady_clock::now(); };
}

WallClock wall_clock() {
    return [] { return std::time(nullptr); };
}

const char* to_string(Credential c) { return c == Credential::Jwt ? "jwt" : "pat"; }

bool looks_like_mcp_token(const std::string& token) {
    static const std::string kPrefix = "wmcp_";
    if (token.size() != kPrefix.size() + 43 || token.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    for (std::size_t i = kPrefix.size(); i < token.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(token[i]);
        if (std::isalnum(c) == 0 && c != '-' && c != '_') return false;
    }
    return true;
}

std::string bearer_token(const std::string& header) {
    // Схема — без учёта регистра (RFC 7235), пробелы после неё схлопываем.
    if (header.size() < 7) return {};
    std::string scheme = header.substr(0, 6);
    for (auto& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (scheme != "bearer" || header[6] != ' ') return {};
    std::size_t start = header.find_first_not_of(' ', 7);
    if (start == std::string::npos) return {};
    std::size_t end = header.find_last_not_of(' ');
    return header.substr(start, end - start + 1);
}

std::string token_key(const std::string& token) { return to_hex(crypto::sha256(token)); }

KeyLookup lazy_jwks(const std::string& auth_service_url) {
    struct State {
        std::string url;
        std::once_flag once;
        std::unique_ptr<jwks::Cache> cache;
    };
    auto state = std::make_shared<State>();
    state->url = auth_service_url;
    return [state](const std::string& kid) -> std::optional<jwks::PublicKey> {
        // Конструктор jwks::Cache сам загружает ключи (и не бросает при
        // недоступном google-auth); дальше find() догружает по новому kid.
        std::call_once(state->once, [&state] { state->cache = std::make_unique<jwks::Cache>(state->url); });
        return state->cache->find(kid);
    };
}

std::string actor_chain(const nlohmann::json& payload) {
    std::string out;
    const nlohmann::json* node = &payload;
    for (int depth = 0; depth < kMaxActorDepth; ++depth) {
        auto act = node->find("act");
        if (act == node->end() || !act->is_object()) break;
        auto sub = act->find("sub");
        if (sub == act->end() || !sub->is_string()) break;
        out += (out.empty() ? "" : ",") + sub->get<std::string>();
        node = &*act;
    }
    return out;
}

VerifiedJwt verify_jwt(const std::string& token, const KeyLookup& keys) {
    if (!keys) return rejected("jwt_disabled");
    std::optional<std::string> kid = jwt::peek_kid(token);
    if (!kid) return rejected("jwt_malformed");
    std::optional<jwks::PublicKey> key = keys(*kid);
    if (!key) return rejected("jwt_unknown_key");

    jwt::VerifyError err = jwt::VerifyError::Malformed;
    std::optional<nlohmann::json> payload = jwt::verify_rs256(token, key->n, key->e, &err);
    if (!payload) {
        switch (err) {
        case jwt::VerifyError::Malformed:
            return rejected("jwt_malformed");
        case jwt::VerifyError::BadSignature:
            return rejected("jwt_signature");
        case jwt::VerifyError::Expired:
            return rejected("jwt_expired");
        }
        return rejected("jwt_malformed");
    }

    jwt_claims::Result claims = jwt_claims::check(*payload, kJwtAudience, kJwtScope);
    if (claims != jwt_claims::Result::Ok) return rejected(std::string("jwt_") + jwt_claims::to_string(claims));

    auto sub = payload->find("sub");
    if (sub == payload->end() || !sub->is_string() || sub->get<std::string>().empty()) return rejected("jwt_no_sub");

    VerifiedJwt v;
    v.ok = true;
    v.user_id = sub->get<std::string>();
    // exp уже проверен verify_rs256: есть и это число в будущем.
    v.exp = static_cast<std::time_t>(payload->at("exp").get<std::uint64_t>());
    v.actor = actor_chain(*payload);
    return v;
}

TokenCache::TokenCache(auth::AuthApi& api, Clock clock, WallClock wall, std::string audience)
    : api_(api), clock_(std::move(clock)), wall_(std::move(wall)), audience_(std::move(audience)) {}

Session TokenCache::get(const Presented& presented) {
    std::string key = token_key(presented.token);
    auto now = clock_();
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (++calls_ % kSweepEvery == 0) sweep_locked(now);
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            if (now < it->second.valid_until) {
                metrics::inc_counter(audience_.empty() ? kCacheMetric : kDocsCacheMetric,
                                     audience_.empty() ? kCacheHelp : kDocsCacheHelp, "result", "hit");
                return {auth::Exchange::Result::Ok, key, it->second.jwt, it->second.user_id};
            }
            entries_.erase(it);
        }
    }
    metrics::inc_counter(audience_.empty() ? kCacheMetric : kDocsCacheMetric,
                         audience_.empty() ? kCacheHelp : kDocsCacheHelp, "result", "miss");
    // Обмен — вне мьютекса: параллельные промахи по одному токену дадут два
    // обмена, это допустимо, а остальные токены не ждут google-auth.
    return exchange(presented, key);
}

Session TokenCache::refresh(const Presented& presented) {
    std::string key = token_key(presented.token);
    {
        std::lock_guard<std::mutex> lock(mu_);
        entries_.erase(key);
    }
    return exchange(presented, key);
}

std::size_t TokenCache::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return entries_.size();
}

Session TokenCache::exchange(const Presented& presented, const std::string& key) {
    const bool is_jwt = presented.kind == Credential::Jwt;
    auth::Exchange ex = is_jwt ? api_.exchange_jwt(presented.token, audience_) : api_.exchange(presented.token, audience_);
    metrics2::inc_counter(audience_.empty() ? kExchangeMetric : kDocsExchangeMetric,
                          audience_.empty() ? kExchangeHelp : kDocsExchangeHelp,
                          {{"result", auth::to_string(ex.result)}, {"kind", to_string(presented.kind)}});
    Session s;
    s.result = ex.result;
    s.key = key;
    if (ex.result != auth::Exchange::Result::Ok) {
        if (ex.invalid_client) {
            applog::error("обмен JWT отклонён google-auth: неверный AUTH_EXCHANGE_SECRET",
                          {{"event", "token_exchange_invalid_client"}});
        } else if (ex.result == auth::Exchange::Result::Unavailable) {
            applog::warn("обмен токена не удался: " + ex.error,
                         {{"event", "mcp_token_exchange_unavailable"}, {"credential", to_string(presented.kind)}});
        }
        return s;
    }
    s.jwt = ex.access_token;
    s.user_id = is_jwt ? presented.user_id : ex.user_id;

    // Срок записи — min(exp JWT для wallet-report, exp входящего JWT) − 30 с:
    // кеш не продлевает входящий JWT (D3).
    auto lifetime = std::chrono::seconds(ex.expires_in);
    if (is_jwt && presented.not_after > 0) {
        auto remaining = std::chrono::seconds(static_cast<long long>(presented.not_after - wall_()));
        lifetime = std::min(lifetime, remaining);
    }
    auto ttl = lifetime - kExpiryMargin;
    if (ttl > std::chrono::seconds(0)) {
        std::lock_guard<std::mutex> lock(mu_);
        entries_[key] = Entry{ex.access_token, s.user_id, clock_() + ttl};
    }
    return s;
}

void TokenCache::sweep_locked(std::chrono::steady_clock::time_point now) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now >= it->second.valid_until) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

UserReports::UserReports(reports::ReportsApi& api, TokenCache& cache, Presented presented, Session session)
    : api_(api), cache_(cache), presented_(std::move(presented)), session_(std::move(session)) {}

reports::Response UserReports::get(const std::string& path_and_query) {
    reports::Response r = api_.get(path_and_query, session_.jwt);
    if (r.status != 401) return r;
    if (retried_) throw AuthRejected();
    retried_ = true;
    Session fresh = cache_.refresh(presented_);
    if (fresh.result == auth::Exchange::Result::Invalid) throw AuthRejected();
    if (fresh.result == auth::Exchange::Result::Unavailable) throw AuthUnavailable();
    session_ = std::move(fresh);
    r = api_.get(path_and_query, session_.jwt);
    if (r.status == 401) throw AuthRejected();
    return r;
}

} // namespace mcpaccess

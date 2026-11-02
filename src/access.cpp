#include "access.hpp"

#include <cctype>

#include "util/crypto.hpp"
#include "util/log.hpp"
#include "util/metrics.hpp"

namespace mcpaccess {

const char* const kInvalidTokenMessage =
    "MCP-токен недействителен или истёк — выпустите новый в веб-кабинете: Настройки → Устройства";
const char* const kAuthUnavailableMessage = "Сервис авторизации временно недоступен, попробуйте позже";

namespace {

constexpr const char* kExchangeMetric = "mcp_token_exchange_total";
constexpr const char* kExchangeHelp = "Обмены MCP-токена на JWT в google-auth по исходу.";
constexpr const char* kCacheMetric = "mcp_token_cache_total";
constexpr const char* kCacheHelp = "Обращения к кешу JWT по MCP-токену: hit/miss.";
constexpr unsigned kSweepEvery = 100;

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

} // namespace

Clock steady_clock() {
    return [] { return std::chrono::steady_clock::now(); };
}

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

std::string token_key(const std::string& mcp_token) { return to_hex(crypto::sha256(mcp_token)); }

TokenCache::TokenCache(auth::AuthApi& api, Clock clock) : api_(api), clock_(std::move(clock)) {}

Session TokenCache::get(const std::string& mcp_token) {
    std::string key = token_key(mcp_token);
    auto now = clock_();
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (++calls_ % kSweepEvery == 0) sweep_locked(now);
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            if (now < it->second.valid_until) {
                metrics::inc_counter(kCacheMetric, kCacheHelp, "result", "hit");
                return {auth::Exchange::Result::Ok, key, it->second.jwt, it->second.user_id};
            }
            entries_.erase(it);
        }
    }
    metrics::inc_counter(kCacheMetric, kCacheHelp, "result", "miss");
    // Обмен — вне мьютекса: параллельные промахи по одному токену дадут два
    // обмена, это допустимо (D3), а остальные токены не ждут google-auth.
    return exchange(mcp_token, key);
}

Session TokenCache::refresh(const std::string& mcp_token) {
    std::string key = token_key(mcp_token);
    {
        std::lock_guard<std::mutex> lock(mu_);
        entries_.erase(key);
    }
    return exchange(mcp_token, key);
}

std::size_t TokenCache::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return entries_.size();
}

Session TokenCache::exchange(const std::string& mcp_token, const std::string& key) {
    auth::Exchange ex = api_.exchange(mcp_token);
    metrics::inc_counter(kExchangeMetric, kExchangeHelp, "result", auth::to_string(ex.result));
    Session s;
    s.result = ex.result;
    s.key = key;
    if (ex.result != auth::Exchange::Result::Ok) {
        if (ex.result == auth::Exchange::Result::Unavailable) {
            applog::warn("обмен MCP-токена не удался: " + ex.error, {{"event", "mcp_token_exchange_unavailable"}});
        }
        return s;
    }
    s.jwt = ex.access_token;
    s.user_id = ex.user_id;
    auto ttl = std::chrono::seconds(ex.expires_in) - kExpiryMargin;
    if (ttl > std::chrono::seconds(0)) {
        std::lock_guard<std::mutex> lock(mu_);
        entries_[key] = Entry{ex.access_token, ex.user_id, clock_() + ttl};
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

UserReports::UserReports(reports::ReportsApi& api, TokenCache& cache, std::string mcp_token, Session session)
    : api_(api), cache_(cache), mcp_token_(std::move(mcp_token)), session_(std::move(session)) {}

reports::Response UserReports::get(const std::string& path_and_query) {
    reports::Response r = api_.get(path_and_query, session_.jwt);
    if (r.status != 401) return r;
    if (retried_) throw AuthRejected();
    retried_ = true;
    Session fresh = cache_.refresh(mcp_token_);
    if (fresh.result == auth::Exchange::Result::Invalid) throw AuthRejected();
    if (fresh.result == auth::Exchange::Result::Unavailable) throw AuthUnavailable();
    session_ = std::move(fresh);
    r = api_.get(path_and_query, session_.jwt);
    if (r.status == 401) throw AuthRejected();
    return r;
}

} // namespace mcpaccess

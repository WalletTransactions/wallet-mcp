#include "directory.hpp"

namespace directory {

std::optional<std::vector<Currency>> parse_currencies(const nlohmann::json& body) {
    if (!body.is_object() || !body.contains("currencies") || !body["currencies"].is_array()) return std::nullopt;
    std::vector<Currency> out;
    for (const auto& c : body["currencies"]) {
        if (!c.is_object() || !c.contains("iso_code") || !c["iso_code"].is_string()) continue;
        Currency cur;
        cur.iso_code = c["iso_code"].get<std::string>();
        if (c.contains("precision") && c["precision"].is_number_integer()) cur.precision = c["precision"].get<int>();
        if (c.contains("show_in_equivalent") && c["show_in_equivalent"].is_boolean()) {
            cur.show_in_equivalent = c["show_in_equivalent"].get<bool>();
        }
        out.push_back(std::move(cur));
    }
    return out;
}

Cache::Cache(mcpaccess::Clock clock) : clock_(std::move(clock)) {}

std::optional<resolver::Accounts> Cache::accounts(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto now = clock_();
    if (++calls_ % 100 == 0) sweep_locked(now);
    auto it = accounts_.find(key);
    if (it == accounts_.end() || now >= it->second.until) return std::nullopt;
    return it->second.value;
}

void Cache::put_accounts(const std::string& key, resolver::Accounts a) {
    std::lock_guard<std::mutex> lock(mu_);
    accounts_[key] = {std::move(a), clock_() + kTtl};
}

std::optional<std::vector<Currency>> Cache::currencies(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = currencies_.find(key);
    if (it == currencies_.end() || clock_() >= it->second.until) return std::nullopt;
    return it->second.value;
}

void Cache::put_currencies(const std::string& key, std::vector<Currency> c) {
    std::lock_guard<std::mutex> lock(mu_);
    currencies_[key] = {std::move(c), clock_() + kTtl};
}

void Cache::drop_accounts(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    accounts_.erase(key);
}

void Cache::sweep_locked(std::chrono::steady_clock::time_point now) {
    for (auto it = accounts_.begin(); it != accounts_.end();) {
        it = now >= it->second.until ? accounts_.erase(it) : std::next(it);
    }
    for (auto it = currencies_.begin(); it != currencies_.end();) {
        it = now >= it->second.until ? currencies_.erase(it) : std::next(it);
    }
}

} // namespace directory

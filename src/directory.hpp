// Справочник пользователя — кошельки и категории (GET /api/reports/accounts)
// и валюты (GET /api/reports/currencies), кеш на 60 с по SHA-256 MCP-токена
// (design D9): почти каждый инструмент сначала разрешает имена или валюту.
#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "access.hpp"
#include "expense/resolver.hpp"

namespace directory {

struct Currency {
    std::string iso_code;
    int precision = 2;
    bool show_in_equivalent = true;
};

// Ответ GET /api/reports/currencies: справочник валют и валюта эквивалента,
// сохранённая пользователем в настройках (change user-eq-currency).
struct Currencies {
    std::vector<Currency> list;
    // ISO-код; nullopt — не выбрана (null) или старый wallet-report без поля.
    std::optional<std::string> preferred_eq_currency;
};

// nullopt — ответ не по контракту.
std::optional<Currencies> parse_currencies(const nlohmann::json& body);

class Cache {
public:
    static constexpr std::chrono::seconds kTtl{60};

    explicit Cache(mcpaccess::Clock clock = mcpaccess::steady_clock());

    std::optional<resolver::Accounts> accounts(const std::string& key);
    void put_accounts(const std::string& key, resolver::Accounts a);
    std::optional<Currencies> currencies(const std::string& key);
    void put_currencies(const std::string& key, Currencies c);
    // Имя не нашлось — справочник могли только что поменять в приложении.
    void drop_accounts(const std::string& key);

private:
    template <typename T>
    struct Slot {
        T value;
        std::chrono::steady_clock::time_point until;
    };
    void sweep_locked(std::chrono::steady_clock::time_point now);

    mcpaccess::Clock clock_;
    std::mutex mu_;
    std::unordered_map<std::string, Slot<resolver::Accounts>> accounts_;
    std::unordered_map<std::string, Slot<Currencies>> currencies_;
    unsigned calls_ = 0;
};

} // namespace directory

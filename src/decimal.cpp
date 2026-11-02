#include "decimal.hpp"

#include <algorithm>

namespace decimal {

namespace {

constexpr int kMaxIntDigits = 28; // 10^36 < 2^127 ≈ 1.7·10^38 с запасом на сложение

__int128 pow10(int n) {
    __int128 p = 1;
    for (int i = 0; i < n; ++i) p *= 10;
    return p;
}

std::string digits_of(__int128 v) {
    if (v == 0) return "0";
    std::string out;
    while (v > 0) {
        out.push_back(static_cast<char>('0' + static_cast<int>(v % 10)));
        v /= 10;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// |units| с ровно digits знаками после точки (units уже на этом масштабе).
std::string render(__int128 units, int digits) {
    bool negative = units < 0;
    __int128 abs = negative ? -units : units;
    __int128 p = pow10(digits);
    std::string out = digits_of(abs / p);
    if (digits > 0) {
        std::string frac = digits_of(abs % p);
        out += "." + std::string(static_cast<std::size_t>(digits) - frac.size(), '0') + frac;
    }
    if (negative && abs != 0) out = "-" + out;
    return out;
}

} // namespace

std::optional<Decimal> parse(const std::string& s) {
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && s[i] == '-') {
        negative = true;
        ++i;
    }
    std::size_t int_start = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    std::size_t int_len = i - int_start;
    if (int_len == 0 || int_len > static_cast<std::size_t>(kMaxIntDigits)) return std::nullopt;

    std::size_t frac_len = 0;
    std::size_t frac_start = 0;
    if (i < s.size() && s[i] == '.') {
        ++i;
        frac_start = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        frac_len = i - frac_start;
        if (frac_len == 0 || frac_len > static_cast<std::size_t>(kScale)) return std::nullopt;
    }
    if (i != s.size()) return std::nullopt;

    __int128 units = 0;
    for (std::size_t k = int_start; k < int_start + int_len; ++k) units = units * 10 + (s[k] - '0');
    for (std::size_t k = 0; k < static_cast<std::size_t>(kScale); ++k) {
        units = units * 10 + (k < frac_len ? s[frac_start + k] - '0' : 0);
    }
    Decimal d;
    d.units = negative ? -units : units;
    d.digits = static_cast<int>(frac_len);
    return d;
}

Decimal add(const Decimal& a, const Decimal& b) { return {a.units + b.units, std::max(a.digits, b.digits)}; }

Decimal sub(const Decimal& a, const Decimal& b) { return {a.units - b.units, std::max(a.digits, b.digits)}; }

std::string to_string(const Decimal& d) {
    // Значение точно представимо с d.digits знаками: каждое слагаемое было
    // не точнее своего digits, а сумма не точнее максимального.
    return render(d.units / pow10(kScale - d.digits), d.digits);
}

std::string format(const Decimal& d, int precision) {
    precision = std::clamp(precision, 0, kScale);
    __int128 p = pow10(kScale - precision);
    bool negative = d.units < 0;
    __int128 abs = negative ? -d.units : d.units;
    __int128 q = abs / p;
    if ((abs % p) * 2 >= p) ++q;
    return render(negative ? -q : q, precision);
}

std::string format_string(const std::string& s, int precision) {
    auto d = parse(s);
    return d ? format(*d, precision) : s;
}

} // namespace decimal

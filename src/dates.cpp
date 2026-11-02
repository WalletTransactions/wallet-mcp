#include "dates.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <tuple>

namespace dates {

namespace {

bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int days_in_month(int y, int m) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && is_leap(y) ? 29 : kDays[m - 1];
}

// Дни от 1970-01-01 (H. Hinnant, days_from_civil) и обратно.
long days_from_civil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

Date civil_from_days(long z) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    long d = doy - (153 * mp + 2) / 5 + 1;
    long m = mp + (mp < 10 ? 3 : -9);
    return {static_cast<int>(y + (m <= 2 ? 1 : 0)), static_cast<int>(m), static_cast<int>(d)};
}

} // namespace

std::optional<Date> parse(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
        if (std::isdigit(static_cast<unsigned char>(s[i])) == 0) return std::nullopt;
    }
    Date d{std::stoi(s.substr(0, 4)), std::stoi(s.substr(5, 2)), std::stoi(s.substr(8, 2))};
    if (d.year < 1900 || d.month < 1 || d.month > 12 || d.day < 1 || d.day > days_in_month(d.year, d.month)) {
        return std::nullopt;
    }
    return d;
}

std::string to_string(const Date& d) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.day);
    return buf;
}

bool operator<(const Date& a, const Date& b) {
    return std::tie(a.year, a.month, a.day) < std::tie(b.year, b.month, b.day);
}

bool operator==(const Date& a, const Date& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

Date add_days(const Date& d, int days) { return civil_from_days(days_from_civil(d.year, d.month, d.day) + days); }

int iso_weekday(const Date& d) {
    long z = days_from_civil(d.year, d.month, d.day);
    // 1970-01-01 — четверг (4).
    long w = (z % 7 + 7 + 3) % 7; // 0 — понедельник
    return static_cast<int>(w) + 1;
}

std::string weekday_ru(const Date& d) {
    static const char* kNames[] = {"понедельник", "вторник", "среда",      "четверг",
                                   "пятница",     "суббота", "воскресенье"};
    return kNames[iso_weekday(d) - 1];
}

std::string period_from(const Date& d) { return to_string(d) + " 00:00:00"; }

std::string period_to(const Date& d) { return to_string(d) + " 23:59:59.999999"; }

std::string balance_date_end_of(const Date& d) { return to_string(add_days(d, 1)); }

bool is_valid_tz(const std::string& name, const std::string& zoneinfo_dir) {
    if (name.empty() || name.size() > 64 || name[0] == '/' || name.find("..") != std::string::npos) return false;
    for (unsigned char c : name) {
        if (std::isalnum(c) == 0 && c != '_' && c != '+' && c != '-' && c != '/') return false;
    }
    std::ifstream f(zoneinfo_dir + "/" + name, std::ios::binary);
    char magic[4] = {};
    return f.read(magic, 4) && magic[0] == 'T' && magic[1] == 'Z' && magic[2] == 'i' && magic[3] == 'f';
}

Date today(std::time_t now) {
    std::tm local{};
    localtime_r(&now, &local);
    return {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
}

} // namespace dates

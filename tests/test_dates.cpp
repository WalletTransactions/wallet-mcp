#include <cstdlib>
#include <ctime>

#include "dates.hpp"
#include "test_framework.hpp"

TEST(dates_parse_checks_existence) {
    CHECK(dates::parse("2028-02-29").has_value()); // високосный
    CHECK(!dates::parse("2026-02-29").has_value());
    CHECK(!dates::parse("2100-02-29").has_value()); // кратен 100, не 400
    CHECK(dates::parse("2000-02-29").has_value());
    CHECK(!dates::parse("2026-04-31").has_value());
    CHECK(dates::parse("2026-04-30").has_value());
    CHECK(!dates::parse("2026-02-30").has_value());
    CHECK(!dates::parse("2026-13-01").has_value());
    CHECK(!dates::parse("2026-00-10").has_value());
    CHECK(!dates::parse("2026-9-1").has_value());
    CHECK(!dates::parse("01.09.2026").has_value());
    CHECK(!dates::parse("2026-09-01T00:00").has_value());
    CHECK(!dates::parse("").has_value());
}

TEST(dates_order_and_arithmetic) {
    auto a = *dates::parse("2026-09-30");
    auto b = *dates::parse("2026-10-01");
    CHECK(a < b);
    CHECK(!(b < a));
    CHECK(dates::add_days(a, 1) == b);
    CHECK_EQ(dates::to_string(dates::add_days(*dates::parse("2026-12-31"), 1)), "2027-01-01");
    CHECK_EQ(dates::to_string(dates::add_days(*dates::parse("2028-02-28"), 1)), "2028-02-29");
    CHECK_EQ(dates::to_string(dates::add_days(*dates::parse("2026-03-01"), -1)), "2026-02-28");
}

TEST(dates_bounds_for_wallet_report) {
    auto from = *dates::parse("2026-09-01");
    auto to = *dates::parse("2026-09-30");
    CHECK_EQ(dates::period_from(from), "2026-09-01 00:00:00");
    CHECK_EQ(dates::period_to(to), "2026-09-30 23:59:59.999999");
    // /balance не включает границу: «на конец 30 сентября» — date=10-01.
    CHECK_EQ(dates::balance_date_end_of(to), "2026-10-01");
    CHECK_EQ(dates::balance_date_end_of(*dates::parse("2026-12-31")), "2027-01-01");
}

TEST(dates_weekday) {
    CHECK_EQ(dates::iso_weekday(*dates::parse("2026-10-31")), 6);
    CHECK_EQ(dates::weekday_ru(*dates::parse("2026-10-31")), "суббота");
    CHECK_EQ(dates::weekday_ru(*dates::parse("2026-11-02")), "понедельник");
    CHECK_EQ(dates::weekday_ru(*dates::parse("1970-01-01")), "четверг");
}

TEST(dates_tz_names) {
    CHECK(dates::is_valid_tz("UTC"));
    CHECK(dates::is_valid_tz("Europe/Moscow"));
    CHECK(!dates::is_valid_tz("Mars/Olympus"));
    CHECK(!dates::is_valid_tz("Europe")); // каталог, не пояс
    CHECK(!dates::is_valid_tz("../../etc/passwd"));
    CHECK(!dates::is_valid_tz("/etc/localtime"));
    CHECK(!dates::is_valid_tz(""));
}

TEST(dates_today_in_process_tz) {
    // 2026-10-31 22:30 UTC — в Москве (UTC+3) уже 1 ноября.
    std::time_t t = 1793485800;
    const char* prev = std::getenv("TZ");
    std::string saved = prev != nullptr ? prev : "";
    setenv("TZ", "UTC", 1);
    tzset();
    CHECK_EQ(dates::to_string(dates::today(t)), "2026-10-31");
    setenv("TZ", "Europe/Moscow", 1);
    tzset();
    CHECK_EQ(dates::to_string(dates::today(t)), "2026-11-01");
    if (prev != nullptr) {
        setenv("TZ", saved.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}

TEST(dates_month_ranges) {
    auto d = [](const char* s) { return *dates::parse(s); };
    dates::Range prev = dates::previous_month(d("2026-11-04"));
    CHECK_EQ(dates::to_string(prev.from), std::string("2026-10-01"));
    CHECK_EQ(dates::to_string(prev.to), std::string("2026-10-31"));
    dates::Range cur = dates::this_month(d("2026-11-04"));
    CHECK_EQ(dates::to_string(cur.from), std::string("2026-11-01"));
    CHECK_EQ(dates::to_string(cur.to), std::string("2026-11-04"));
    // Через Новый год и в феврале високосного года.
    prev = dates::previous_month(d("2027-01-15"));
    CHECK_EQ(dates::to_string(prev.from), std::string("2026-12-01"));
    CHECK_EQ(dates::to_string(prev.to), std::string("2026-12-31"));
    CHECK_EQ(dates::to_string(dates::previous_month(d("2028-03-01")).to), std::string("2028-02-29"));
    CHECK_EQ(dates::month_name_ru(10), std::string("октябрь"));
    CHECK_EQ(dates::month_name_ru(13), std::string(""));
}

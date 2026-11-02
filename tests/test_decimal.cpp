#include "decimal.hpp"
#include "test_framework.hpp"

namespace {

decimal::Decimal d(const char* s) {
    auto v = decimal::parse(s);
    REQUIRE(v.has_value());
    return *v;
}

} // namespace

TEST(decimal_kopecks_sum_exactly) {
    auto sum = decimal::add(decimal::add(d("0.10"), d("0.20")), d("1999.99"));
    CHECK_EQ(decimal::to_string(sum), "2000.29");
    CHECK_EQ(decimal::format(sum, 2), "2000.29");
    // Как у double — ровно 0.3, без хвоста.
    CHECK_EQ(decimal::to_string(decimal::add(d("0.1"), d("0.2"))), "0.3");
}

TEST(decimal_digits_follow_max_of_terms) {
    CHECK_EQ(decimal::to_string(decimal::add(d("5"), d("7"))), "12");
    CHECK_EQ(decimal::to_string(decimal::add(d("5"), d("0.125"))), "5.125");
    CHECK_EQ(decimal::to_string(d("0.12345678")), "0.12345678");
}

TEST(decimal_negative) {
    CHECK_EQ(decimal::to_string(decimal::sub(d("100.00"), d("250.50"))), "-150.50");
    CHECK_EQ(decimal::to_string(decimal::add(d("-1.5"), d("1.5"))), "0.0");
    CHECK_EQ(decimal::format(d("-0.004"), 2), "0.00"); // без «-0.00»
    CHECK_EQ(decimal::format(d("-2.345"), 2), "-2.35");
}

TEST(decimal_twelve_integer_digits) {
    auto sum = decimal::add(d("999999999999.99"), d("0.01"));
    CHECK_EQ(decimal::to_string(sum), "1000000000000.00");
    // numeric(20,8): 12 знаков целой части + 8 дробной.
    CHECK_EQ(decimal::to_string(decimal::add(d("123456789012.12345678"), d("0.00000001"))), "123456789012.12345679");
}

TEST(decimal_rejects_garbage) {
    for (const char* bad : {"", "-", "+1", "1.", ".5", "1e3", " 1", "1 ", "1,5", "abc", "1.123456789", "--1", "0x10",
                            "12345678901234567890123456789"}) {
        if (decimal::parse(bad).has_value()) {
            ::testing::report_failure(std::string("parse должен отказать: \"") + bad + "\"", __FILE__, __LINE__);
        }
    }
    CHECK_EQ(decimal::format_string("n/a", 2), "n/a");
}

TEST(decimal_format_rounds_half_away_from_zero) {
    CHECK_EQ(decimal::format(d("1.005"), 2), "1.01");
    CHECK_EQ(decimal::format(d("1.004999"), 2), "1.00");
    CHECK_EQ(decimal::format(d("32000"), 2), "32000.00");
    CHECK_EQ(decimal::format(d("12.5"), 0), "13");
    CHECK_EQ(decimal::format_string("0", 2), "0.00");
}

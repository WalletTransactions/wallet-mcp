#include "category_themes.hpp"
#include "test_framework.hpp"

TEST(themes_cafe_group_across_languages) {
    CHECK(themes::same_theme("Кафе и рестораны", "Café"));
    CHECK(themes::same_theme("Кафе и рестораны", "Restaurants"));
    CHECK(themes::same_theme("Café", "Restaurants"));
    CHECK(themes::same_theme("КАФЕ", "Café"));
    CHECK(themes::same_theme("Рестораны", "restaurant"));
}

TEST(themes_whole_word_not_prefix) {
    // «кафе» — слово целиком: «Кафель» и «Cafeteria»-подобные не цепляются.
    CHECK_EQ(themes::theme_mask("Кафель"), 0u);
    CHECK(!themes::same_theme("Кафе", "Кафель"));
    CHECK_EQ(themes::theme_mask("Barber"), 0u);
}

TEST(themes_undefined_theme) {
    CHECK_EQ(themes::theme_mask("Няня"), 0u);
    CHECK(!themes::same_theme("Няня", "Няня"));
    CHECK(!themes::same_theme("Няня", "Café"));
}

TEST(themes_other_groups_stay_apart) {
    CHECK(themes::same_theme("Продукты", "Groceries"));
    CHECK(themes::same_theme("Такси", "Taxi"));
    CHECK(themes::same_theme("Аптека", "Врач"));
    CHECK(!themes::same_theme("Продукты", "Café"));
    CHECK(!themes::same_theme("Такси", "Аптека"));
}

TEST(themes_words_split_on_punctuation) {
    CHECK(themes::same_theme("Cafe/Bar", "Кафе"));
    CHECK(themes::same_theme("Кафе на работе", "Café"));
    std::vector<std::string> w = themes::words("Café-Bar (work)");
    CHECK((w == std::vector<std::string>{"cafe", "bar", "work"}));
}

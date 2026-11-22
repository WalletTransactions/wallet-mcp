#include "category_themes.hpp"

#include <cctype>
#include <cstddef>

#include "expense/text.hpp"

namespace themes {

namespace {

struct Group {
    std::vector<std::string> whole;    // слово совпадает целиком
    std::vector<std::string> prefixes; // слово начинается с основы
};

// Слова — в нижнем регистре, после fold. Короткие слова («кафе», `bar`) —
// только целиком: «Кафель» в тему кафе не попадает. Основы — длиннее пяти
// букв, чтобы не цеплять чужие слова.
const std::vector<Group>& groups() {
    static const std::vector<Group> g = {
        // кафе и рестораны
        {{"кафе", "cafe", "bar", "бар", "fastfood", "фастфуд", "кофе", "coffee"},
         {"ресторан", "restaurant", "кофейн", "coffeeshop"}},
        // продукты
        {{"продукты", "groceries", "grocery"}, {"продукт", "grocer", "супермаркет", "supermarket"}},
        // транспорт и такси
        {{"такси", "taxi", "метро", "metro", "проезд", "bus", "автобус"}, {"транспорт", "transport"}},
        // аптека и врач
        {{"аптека", "аптеки", "pharmacy", "врач", "врачи", "doctor", "doctors"},
         {"медицин", "medic", "pharmac", "здоровь"}},
    };
    return g;
}

// Латиница с диакритикой U+00C0..U+00FF → ASCII; '\0' — оставить как есть.
const std::string& latin1_map() {
    static const std::string m = std::string("aaaaaa") + '\0' + "c" + "eeee" + "iiii" + '\0' + "n" + "ooooo" + '\0' +
                                 "o" + "uuuu" + "y" + '\0' + '\0' + // U+00C0..U+00DF
                                 "aaaaaa" + '\0' + "c" + "eeee" + "iiii" + '\0' + "n" + "ooooo" + '\0' + "o" +
                                 "uuuu" + "y" + '\0' + "y"; // U+00E0..U+00FF
    return m;
}

bool word_in_group(const std::string& w, const Group& g) {
    for (const auto& s : g.whole) {
        if (w == s) return true;
    }
    for (const auto& p : g.prefixes) {
        if (w.compare(0, p.size(), p) == 0) return true;
    }
    return false;
}

} // namespace

std::string fold(const std::string& name) {
    const std::string n = text::normalize_name(name);
    const std::string& map = latin1_map();
    std::string out;
    out.reserve(n.size());
    for (std::size_t i = 0; i < n.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(n[i]);
        if (c == 0xC3 && i + 1 < n.size()) {
            unsigned char b = static_cast<unsigned char>(n[i + 1]);
            if (b >= 0x80 && b <= 0xBF) {
                char mapped = map[b - 0x80];
                if (mapped != '\0') {
                    out.push_back(mapped);
                    ++i;
                    continue;
                }
            }
        }
        out.push_back(n[i]);
    }
    return out;
}

std::vector<std::string> words(const std::string& name) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : fold(name)) {
        bool letter = c >= 0x80 || std::isalnum(c) != 0;
        if (letter) {
            cur.push_back(static_cast<char>(c));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

unsigned theme_mask(const std::string& name) {
    unsigned mask = 0;
    const auto ws = words(name);
    const auto& g = groups();
    for (std::size_t i = 0; i < g.size(); ++i) {
        for (const auto& w : ws) {
            if (word_in_group(w, g[i])) {
                mask |= 1u << i;
                break;
            }
        }
    }
    return mask;
}

bool same_theme(const std::string& a, const std::string& b) { return (theme_mask(a) & theme_mask(b)) != 0; }

} // namespace themes

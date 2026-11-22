// Темы категорий (change add-similar-categories-hint): небольшая встроенная
// таблица групп слов на русском и английском, чтобы понять, что «Кафе и
// рестораны», «Café» и «Restaurants» — одна тема. Это сопоставление «одной
// темы» для подсказки `similar`, а не поиск имени: text::resolve_name из
// wallet-common не меняется.
#pragma once

#include <string>
#include <vector>

namespace themes {

// text::normalize_name (регистр, ё → е) плюс латиница с диакритикой в ASCII:
// «Café» → "cafe".
std::string fold(const std::string& name);

// Слова названия после fold: разделители — любые ASCII-знаки, кроме букв и
// цифр («Кафе и рестораны», «Cafe/Bar» → по словам). Байты UTF-8 — буквы.
std::vector<std::string> words(const std::string& name);

// Маска групп, к которым относится название; 0 — тема не определена.
// Слово принадлежит группе, если равно одному из её слов целиком («кафе»,
// `bar`) или начинается с её основы («ресторан», `restaurant`).
unsigned theme_mask(const std::string& name);

// У двух названий есть общая группа.
bool same_theme(const std::string& a, const std::string& b);

} // namespace themes

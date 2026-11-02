// Точная десятичная арифметика над суммами из wallet-report (design D10):
// итог по выбранным категориям, delta и total_before складывает сервис, а
// double для денег не годится (0.1 + 0.2 = 0.30000000000000004).
//
// Значение — целое __int128 с масштабом 10^8: operation.amount —
// numeric(20,8), 20 знаков целиком влезают в 128 бит. Складываются только
// строки, которые уже посчитал SQL, — сервис сводит выборку, а не считает
// суммы по сырым операциям.
#pragma once

#include <optional>
#include <string>

namespace decimal {

constexpr int kScale = 8;

struct Decimal {
    __int128 units = 0; // значение × 10^8
    int digits = 0;     // знаков после точки во входе (для вывода суммы)
};

// "[-]цифры[.цифры]": до 28 цифр целой части, до 8 — дробной. Пустая
// строка, "+", пробелы, экспонента, лишние знаки после 8-го — nullopt.
std::optional<Decimal> parse(const std::string& s);

Decimal add(const Decimal& a, const Decimal& b);
Decimal sub(const Decimal& a, const Decimal& b);

// Число знаков — максимальное у слагаемых (digits), без хвостовой точки:
// "0.10" + "0.20" + "1999.99" → "2000.29", "5" + "7" → "12".
std::string to_string(const Decimal& d);

// Округление до precision знаков (половина — от нуля) и вывод ровно с
// precision знаками: суммы в ответе модели — по precision валюты (D11).
std::string format(const Decimal& d, int precision);

// parse + format; непарсящаяся строка возвращается как есть.
std::string format_string(const std::string& s, int precision);

} // namespace decimal

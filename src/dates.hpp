// Даты аргументов инструментов и границы запросов в wallet-report (design
// D6, D7). Существование даты проверяет код, а не Postgres: модель должна
// получить понятный isError, а не «Отчёты временно недоступны».
//
// Пояс «сегодня» — один на сервис (MCP_TZ): main() выставляет его в TZ
// процесса один раз при старте, до первого потока, поэтому localtime_r
// дальше потокобезопасен.
#pragma once

#include <ctime>
#include <optional>
#include <string>

namespace dates {

struct Date {
    int year = 0;
    int month = 0;
    int day = 0;
};

// Ровно "ГГГГ-ММ-ДД" и такой день существует (29 февраля — только в
// високосный год).
std::optional<Date> parse(const std::string& s);

std::string to_string(const Date& d);

bool operator<(const Date& a, const Date& b);
bool operator==(const Date& a, const Date& b);

Date add_days(const Date& d, int days);

// 1 — понедельник … 7 — воскресенье.
int iso_weekday(const Date& d);
// «понедельник» … «воскресенье».
std::string weekday_ru(const Date& d);

// Границы периода wallet-report включительны (periodFrom/periodTo):
// "ГГГГ-ММ-ДД 00:00:00" и "ГГГГ-ММ-ДД 23:59:59.999999".
std::string period_from(const Date& d);
std::string period_to(const Date& d);

// /balance не включает свою границу, а инструмент обещает баланс на конец
// дня, поэтому в запрос уходит следующий день.
std::string balance_date_end_of(const Date& d);

// Пояс IANA вида "Europe/Moscow": только [A-Za-z0-9_+-/], без "..", без
// ведущего '/', и файл zoneinfo_dir/<name> — файл пояса (TZif).
bool is_valid_tz(const std::string& name, const std::string& zoneinfo_dir = "/usr/share/zoneinfo");

// Сегодня в поясе процесса (TZ, см. выше).
Date today(std::time_t now);

} // namespace dates

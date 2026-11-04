# Tasks

## 1. Периоды и имена

- [x] 1.1 `dates::this_month`, `dates::previous_month`, `dates::month_name_ru`; `instructions` с готовыми периодами и правилом про скобки. Проверка: `tests/test_dates.cpp`: `dates_month_ranges` (ноябрь → октябрь, январь → декабрь прошлого года, февраль високосного); `tests/test_mcp.cpp`: `mcp_instructions_text` (2026-11-04 — октябрь, без `2026-09-01`)
- [x] 1.2 `tools::without_parenthetical` и повтор поиска в `resolve`. Проверка: `tests/test_tools.cpp`: `tools_name_with_parenthetical_note` («Продукты (еда)» находится, «каф (кофе)» — по-прежнему неоднозначно, ошибка с переданным именем)

## 2. Живая проверка

- [ ] 2.1 На test в боте: «сколько за няню?» → «давай прошлый месяц». Проверка: ответ за 2026-10-01…2026-10-31, сумма совпадает с `/api/reports/expense-structure` за октябрь

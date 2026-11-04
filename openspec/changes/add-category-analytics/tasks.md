# Tasks

## 1. Арифметика и даты

- [ ] 1.1 `decimal::div(a, b, precision)` с округлением половины от нуля, `nullopt` при нуле и переполнении (design D4). Проверка: `ctest` — `test_decimal.cpp`: 1304.09 / 24.00 = 54.34, 27850.00 / 9.00 = 3094.44, 300.00 / 0.50 = 600.00, −1 / 3 → −0.33, деление на 0 → `nullopt`
- [ ] 1.2 `dates::month_fractions(from, to)` → список `{год, месяц, доля в сотых}` и `dates::months_touched(from, to)` (D3). Проверка: `ctest` — `test_dates.cpp`: 2024-11-05..2026-11-04 → 25 месяцев и сумма 24.00; 2026-09-01..2026-09-15 → 0.50; февраль високосного года; период в один день

## 2. Исключение категорий

- [ ] 2.1 Аргумент `exclude_categories` (names_prop, до 50) в схемах `spending_by_category` и `search_operations`; проверка совместного задания с `categories` — `isError` «Задайте categories или exclude_categories, не оба.» до wallet-report (D5). Проверка: `ctest` — `test_tools.cpp`: оба списка → текст ошибки, `FakeReports` не вызван
- [ ] 2.2 `spending_by_category`: строки без исключённых, `total` — точная сумма оставшихся. `search_operations`: `exclude_category_uuid` в запросе, итог при любом `kind`. Проверка: `ctest` — `tools_spending_exclude_categories` (строк нет, итог по остальным) и `tools_search_exclude_categories` (параметр ушёл в `FakeReports`, `total` из ответа)
- [ ] 2.3 README: аргументы в строках `spending_by_category` и `search_operations` таблицы инструментов. Проверка: таблица совпадает с `inputSchema` из `tools/list` (сравнить вручную с выводом curl на локальном запуске)

## 3. Число операций и среднее в spending_by_category

- [ ] 3.1 `count` из `op_count` (нет поля — `null`), `months` и `avg_per_month` у строк и итога; с периодом сравнения — `count_before`, `avg_per_month_before`, `months_before` (D3, D7). Существующие ключи и порядок прежние. Проверка: `ctest` — `tools_spending_count_and_average` (сценарий «Лето против девяти месяцев» из spec: 1911.00 и 3094.44), `tools_spending_without_op_count_from_old_report` (`count: null`, остальное как раньше); старые `tools_spending_*` зелёные без правок
- [ ] 3.2 Описание инструмента в реестре упоминает `count`, `avg_per_month`, `months`; README — колонка ответа. Проверка: `tools/list` на локальном запуске показывает новый текст

## 4. spending_by_category_by_month

- [ ] 4.1 `FakeReports`: маршрут `/api/reports/expense-by-category-by-month` с ответом по форме `openapi.yaml` wallet-report (`reportExpenseByCategoryByMonth`). Проверка: тест-хелпер собирается, `ctest` зелёный
- [ ] 4.2 Инструмент: схема (`from`, `to`, `categories?`, `exclude_categories?`, `months_of_year?` — integer 1..12, `uniqueItems`, `currency?`), лимит 36 месяцев с текстом «Период — не длиннее 36 месяцев. Разбейте его на части.», «В периоде нет выбранных месяцев.», оба текста — до wallet-report. Проверка: `ctest` — `tools_by_month_period_limit`, `tools_by_month_no_selected_months`, `[13]` и повтор → `-32602`
- [ ] 4.3 Свод ответа: фильтр клеток по `months_of_year` (D2), `amount`/`count`/`avg_per_month` у строк, `by_month` только при `categories` (D6), `total` — `total_eq` без фильтров или точная сумма клеток, `months` и `avg_per_month` итога, `incomplete`, эхо `period`/`currency`/`months_of_year`. Проверка: `ctest` — `tools_by_month_alcohol_two_years` (сценарий spec), `tools_by_month_summer_plus_rest_equals_whole` (сумма двух `total` = итог без фильтра), `tools_by_month_by_month_only_with_categories`, `tools_by_month_exclude`
- [ ] 4.4 Регистрация в диспетчере, метрики `mcp_tool_calls_total{tool="spending_by_category_by_month"}`, требование «Состав инструментов» — семь. Проверка: `ctest` — тест `tools/list` ждёт семь имён; `/metrics` на локальном запуске после вызова содержит метку нового инструмента
- [ ] 4.5 README: строка нового инструмента в таблице инструментов и в таблице вызовов wallet-report. Проверка: строки совпадают с `inputSchema` и картой D1

## 5. Инструкции

- [ ] 5.1 `instructions`: строка «Среднее в месяц бери из avg_per_month, сравнение месяцев года (лето, зима) — через months_of_year в spending_by_category_by_month; сам не дели и не складывай.» Проверка: `ctest` — `test_mcp.cpp` проверяет строку в ответе `initialize`, старые проверки дат зелёные

## 6. Живой стек

- [ ] 6.1 После выкладки парного change wallet-report на test — вызов через гейтвей (`/api/mcp/`) на леджере владельца: `spending_by_category_by_month(2024-11-05, 2026-11-04, categories: ["Alcohol"])` даёт `amount` 1304.09 (как `search_operations` 2026-11-04) и `months` 24.00; два вызова с `months_of_year` [6,7,8] и остальными месяцами за 2024-09-01..2026-08-31 с `exclude_categories` ремонта дают `total`, в сумме равные вызову без `months_of_year`. Проверка: вывод curl приложен к тест-задаче
- [ ] 6.2 Живой прогон из LM Studio: вопросы «сколько в среднем в месяц я трачу на алкоголь за два года» и «насколько июнь–август дороже остальных месяцев по категориям, без ремонта». Проверка: в логе wallet-mcp модель вызвала `spending_by_category_by_month` с `months_of_year` и `exclude_categories`, ответ модели называет суммы из `avg_per_month`

## Workflow follow-up

- Выкатывается после парного change wallet-report `add-category-month-analytics`; архивировать оба вместе.
- Группы категорий (Alcohol + Bar, «Ремонт квартиры») — отдельный change, начинается с wallet-api.
- Обновить `org/docs/TODO_wallet_ai.md` (пункт 10 — MCP): новые инструменты.

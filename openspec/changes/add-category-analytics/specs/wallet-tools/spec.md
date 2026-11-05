# Spec Delta

## MODIFIED Requirements

### Requirement: Состав инструментов
`tools/list` SHALL возвращать ровно семь инструментов: `list_wallets`,
`list_categories`, `spending_by_category`, `spending_by_category_by_month`,
`income_expense_by_month`, `balance`, `search_operations`. У каждого есть
описание на русском и `inputSchema` (JSON Schema, `additionalProperties:
false`). Инструментов, меняющих данные, нет.

#### Scenario: Список
- **WHEN** клиент вызывает `tools/list`
- **THEN** в ответе семь инструментов с этими именами, и ни один не создаёт, не меняет и не удаляет данные

## ADDED Requirements

### Requirement: spending_by_category_by_month
`spending_by_category_by_month(from, to, categories?, exclude_categories?,
months_of_year?, currency?)` SHALL вернуть расход по категориям из
`GET /api/reports/expense-by-category-by-month`: у строки — `category`,
`amount`, `count`, `avg_per_month`, а при заданных `categories` — `by_month`
(объект «`ГГГГ-ММ` → сумма» только с ненулевыми месяцами); в ответе —
`total`, `count`, `avg_per_month`, `months` и `incomplete`. Без `categories` —
все категории с расходом, без `by_month`.

#### Scenario: Алкоголь за два года
- **WHEN** `from: "2024-11-05"`, `to: "2026-11-04"`, `categories: ["Alcohol"]`, за период 37 покупок на 1304.09
- **THEN** строка «Alcohol» несёт `amount: "1304.09"`, `count: 37`, `months: "24.00"`, `avg_per_month: "54.34"`, а `total` равен `amount`

#### Scenario: Пустые месяцы не выводятся
- **WHEN** `categories: ["Bar"]`, по «Bar» в периоде расход только в июне и августе
- **THEN** `by_month` у «Bar» содержит ровно два ключа — июнь и август

#### Scenario: Все категории без помесячной разбивки
- **WHEN** не заданы ни `categories`, ни `exclude_categories`, ни `months_of_year`
- **THEN** у строк нет `by_month`, а `total` — итог всего расхода периода из wallet-report

### Requirement: Месяцы года
Аргумент `months_of_year` — список номеров месяцев 1..12 без повторов —
SHALL оставлять в расчёте только эти календарные месяцы периода: суммы,
`count`, `by_month` и делитель `months` считаются по ним. Ответ эхом несёт
`months_of_year` по возрастанию. Номер вне 1..12 или повтор — ошибка JSON-RPC
`-32602`.

#### Scenario: Лето против остальных
- **WHEN** период 2024-09-01..2026-08-31, `months_of_year: [6, 7, 8]`
- **THEN** в расчёт входят только июни, июли и августы 2025 и 2026 годов, `months: "6.00"`

#### Scenario: Остальные месяцы
- **WHEN** тот же период, `months_of_year: [1, 2, 3, 4, 5, 9, 10, 11, 12]`
- **THEN** `months: "18.00"`, и сумма `total` двух вызовов равна `total` вызова без `months_of_year`

#### Scenario: Выбранных месяцев нет в периоде
- **WHEN** период 2026-09-01..2026-10-31, `months_of_year: [6, 7, 8]`
- **THEN** `isError: true` с текстом «В периоде нет выбранных месяцев.», wallet-report не вызывается

#### Scenario: Тринадцатый месяц
- **WHEN** `months_of_year: [13]`
- **THEN** ошибка JSON-RPC `-32602`, wallet-report не вызывается

### Requirement: Длина помесячного периода
`spending_by_category_by_month` SHALL принимать период, задевающий не больше
36 календарных месяцев; длиннее — `isError: true` с текстом «Период — не
длиннее 36 месяцев. Разбейте его на части.», wallet-report не вызывается.

#### Scenario: Четыре года
- **WHEN** `from: "2022-01-01"`, `to: "2025-12-31"`
- **THEN** `isError: true` с этим текстом

### Requirement: Делитель среднего — доля месяцев по дням
`months` SHALL быть суммой по месяцам периода (а при `months_of_year` — только
выбранным) долей «дней месяца в периоде / дней в месяце», с двумя знаками
после точки. `avg_per_month` — сумма, делённая на `months`, точной десятичной
арифметикой, с округлением до precision валюты (половина — от нуля).

#### Scenario: Два года со сдвигом
- **WHEN** период 2024-11-05..2026-11-04
- **THEN** `months: "24.00"`: ноябрь 2024 даёт 26/30, ноябрь 2026 — 4/30, остальные месяцы по 1

#### Scenario: Полмесяца
- **WHEN** период 2026-09-01..2026-09-15, расход 300.00
- **THEN** `months: "0.50"`, `avg_per_month: "600.00"`

### Requirement: Исключение категорий
`spending_by_category`, `spending_by_category_by_month` и `search_operations`
SHALL принимать `exclude_categories` — имена категорий по тем же правилам
сопоставления, что `categories`, до 50: в расчёт идут все категории, кроме
них. С `categories` вместе — `isError: true` с текстом «Задайте categories
или exclude_categories, не оба.», wallet-report не вызывается.

#### Scenario: Всё, кроме ремонта
- **WHEN** `spending_by_category`, `exclude_categories: ["Ремонт", "Строй материал", "Мебель"]`
- **THEN** строки — все категории с расходом, кроме трёх, а `total` — точная сумма этих строк

#### Scenario: Поиск без категории
- **WHEN** `search_operations`, `exclude_categories: ["Ремонт"]`
- **THEN** в wallet-report уходит `exclude_category_uuid=<Ремонт>`, `total` — по всем расходам, кроме «Ремонта»

#### Scenario: Оба списка
- **WHEN** заданы `categories: ["Bar"]` и `exclude_categories: ["Alcohol"]`
- **THEN** `isError: true` с этим текстом

### Requirement: Число операций и среднее в spending_by_category
Строка `spending_by_category` SHALL нести `count` (из `op_count`
wallet-report) и `avg_per_month`, ответ — `months` и `avg_per_month` итога,
по правилу делителя выше. С периодом сравнения — также `count_before`,
`avg_per_month_before` у строк и `months_before`, `avg_per_month_before` в
ответе.

#### Scenario: Лето против девяти месяцев
- **WHEN** период 2026-06-01..2026-08-31, сравнение 2025-09-01..2026-05-31, `categories: ["Baby Sitter"]`, суммы 5733.00 и 27850.00
- **THEN** `months: "3.00"`, `months_before: "9.00"`, `avg_per_month: "1911.00"`, `avg_per_month_before: "3094.44"`

#### Scenario: Категория без трат
- **WHEN** выбранная категория существует, но в периоде по ней расходов нет
- **THEN** у строки `amount: "0"`, `count: 0`, `avg_per_month` — ноль с precision валюты

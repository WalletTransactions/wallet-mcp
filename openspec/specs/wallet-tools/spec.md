# wallet-tools Specification

## Purpose
Набор read-only инструментов кошелька для модели MCP-клиента: что каждый
инструмент принимает, откуда берёт данные в wallet-report и в каком виде их
возвращает, чтобы ответы на вопросы о тратах были посчитаны SQL, а не моделью.

## Requirements

### Requirement: Состав инструментов
`tools/list` SHALL возвращать семь инструментов кошелька: `list_wallets`,
`list_categories`, `spending_by_category`, `spending_by_category_by_month`,
`income_expense_by_month`, `balance`, `search_operations` — и, только если задан
`WALLET_DOCS_URL`, восьмой: `ask_docs` (спека `docs-lookup`). У каждого есть описание на
русском и `inputSchema` (JSON Schema, `additionalProperties: false`). Инструментов,
меняющих данные, нет; `ask_docs` данных кошелька не читает.

#### Scenario: Список
- **WHEN** `WALLET_DOCS_URL` пуст, клиент вызывает `tools/list`
- **THEN** в ответе семь инструментов с этими именами, и ни один не создаёт, не меняет и не удаляет данные

#### Scenario: Список со справкой
- **WHEN** `WALLET_DOCS_URL` задан, клиент вызывает `tools/list`
- **THEN** в ответе восемь инструментов: семь прежних и `ask_docs`, ни один не меняет данные

### Requirement: Даты и период
Аргументы `from`, `to`, `compare_from`, `compare_to` и `date` SHALL быть датами
`ГГГГ-ММ-ДД`. Период включает оба дня целиком. Если дата не по формату или не
существует, а также если `from` позже `to`, инструмент возвращает
`isError: true` с текстом, какая дата неверна.

#### Scenario: Сентябрь
- **WHEN** `from: "2026-09-01"`, `to: "2026-09-30"`
- **THEN** в расчёт попадают операции с 2026-09-01 00:00 по 2026-09-30 23:59:59 включительно

#### Scenario: Перепутанные границы
- **WHEN** `from: "2026-09-30"`, `to: "2026-09-01"`
- **THEN** `isError: true`, wallet-report не вызывается

### Requirement: Валюта эквивалента
Инструменты с суммами SHALL принимать необязательный `currency` (ISO-код).
Если он не задан, используется валюта первого кошелька из справочника
`GET /api/reports/accounts`. Неизвестный код приводит к `isError: true`. Если
кошельков нет, возвращается `isError: true` с текстом «Кошельков пока нет —
заведите их в приложении.».

#### Scenario: Валюта по умолчанию
- **WHEN** у пользователя кошельки «Наличные» (RUB) и «Revolut» (EUR) в этом порядке справочника, `currency` не задан
- **THEN** суммы считаются в RUB, и в ответе `currency: "RUB"`

### Requirement: Эхо условий расчёта
Каждый успешный ответ инструмента с суммами SHALL содержать `currency`, а
ответ инструмента с периодом — `period` `{"from","to"}` (и `compare_period`,
если он задан) в виде, в котором они ушли в расчёт.

#### Scenario: Проверка понимания «прошлого месяца»
- **WHEN** модель считает «прошлый месяц» за август и вызывает инструмент с `2026-08-01`..`2026-08-31`
- **THEN** ответ несёт `period: {"from":"2026-08-01","to":"2026-08-31"}`

### Requirement: Имена вместо идентификаторов
Аргументы с кошельками и категориями SHALL принимать имена. Имя сопоставляется
без учёта регистра (ASCII и кириллица), с `ё` = `е` и схлопнутыми пробелами:
сначала точное совпадение, затем уникальный префикс. Системные категории
(Transfer и т. п.) не участвуют. Если имя не нашлось и кончается пояснением
в скобках, оно сопоставляется ещё раз без пояснения; ошибки — с именем, как
его передали.

#### Scenario: Префикс
- **WHEN** категории «Продукты» и «Проезд», аргумент `"прод"`
- **THEN** выбирается «Продукты»

#### Scenario: Пояснение в скобках
- **WHEN** категория «Baby Sitter», аргумент `"Baby Sitter (Няня)"`
- **THEN** выбирается «Baby Sitter»

### Requirement: Неоднозначное и ненайденное имя
Если имя подходит к нескольким счетам, инструмент SHALL вернуть `isError: true`
с текстом «"<имя>" подходит к нескольким: <имена через запятую>. Уточните.».
Если не подходит ни к одному — `isError: true` с текстом «"<имя>" не найдено.
Есть: <все имена этого вида через запятую>.».

#### Scenario: Неоднозначность
- **WHEN** категории «Кафе» и «Кафе на работе», аргумент `"каф"`
- **THEN** `isError: true`, текст перечисляет «Кафе», «Кафе на работе», данные не возвращаются

### Requirement: list_wallets
`list_wallets()` SHALL вернуть живые неархивные кошельки (имя, ISO-код
валюты, баланс на конец сегодняшнего дня в валюте кошелька) и итог в
`currency`, с признаком `incomplete`, если курса нет хотя бы у одного
кошелька. Источник — `GET /api/reports/accounts` и `GET /api/reports/balance`.

#### Scenario: Что на карте
- **WHEN** модель вызывает `list_wallets`
- **THEN** у каждого кошелька есть `name`, `currency`, `balance`, и есть общий `total` в `currency`

### Requirement: list_categories
`list_categories()` SHALL вернуть имена живых неархивных категорий без
системных, в порядке справочника.

#### Scenario: Категории
- **WHEN** модель вызывает `list_categories`
- **THEN** в списке нет «Transfer» и архивных категорий

### Requirement: spending_by_category
`spending_by_category(from, to, categories?, compare_from?, compare_to?,
currency?)` SHALL вернуть расход по категориям за период из
`GET /api/reports/expense-structure`. Если `categories` заданы — только эти
строки и `total` по ним, иначе все категории и `total` периода.

#### Scenario: Няня и еда
- **WHEN** `from: "2026-09-01"`, `to: "2026-09-30"`, `categories: ["Няня","Продукты","Кафе"]`
- **THEN** ответ содержит три строки с суммами и `total`, равный их точной сумме

#### Scenario: Категория без трат
- **WHEN** выбранная категория существует, но в периоде по ней расходов нет
- **THEN** её строка присутствует с суммой `"0"`

### Requirement: Сравнение периодов в расходе
При заданных `compare_from` и `compare_to` `spending_by_category` SHALL
добавить к каждой строке `amount_before` и `delta` (`amount` − `amount_before`),
а к ответу — `total_before`. Задана только одна из границ — `isError: true`.

#### Scenario: Сентябрь против августа
- **WHEN** период сентябрь, период сравнения август, категория «Продукты»
- **THEN** строка несёт `amount`, `amount_before` и их разность `delta`

### Requirement: Итоги без потери точности
Суммы, которые складывает или вычитает сам сервис (`total` по выбранным
категориям, `delta`, `total_before`), SHALL считаться точной десятичной
арифметикой над строками wallet-report, без перевода в двоичную плавающую
точку. Если у какой-то слагаемой суммы нет курса, ответ несёт
`incomplete: true`.

#### Scenario: Копейки
- **WHEN** суммы категорий `"0.10"`, `"0.20"` и `"1999.99"`
- **THEN** `total` — `"2000.29"`

#### Scenario: Нет курса
- **WHEN** у одной из выбранных категорий `amount_eq` равен null
- **THEN** она не входит в `total`, а ответ несёт `incomplete: true`

### Requirement: income_expense_by_month
`income_expense_by_month(from, to, currency?)` SHALL вернуть по строке на
каждый календарный месяц периода (`month`, `income`, `expense`) из
`GET /api/reports/income-expense-by-month`, включая месяцы без операций, и
`incomplete`.

#### Scenario: Пустой месяц
- **WHEN** в июле не было операций
- **THEN** строка июля есть, с `income` и `expense`, равными нулю

### Requirement: balance
`balance(wallet?, date?, currency?)` SHALL вернуть баланс на конец дня `date`
(по умолчанию — сегодня) из `GET /api/reports/balance`: по всем кошелькам, а
при заданном `wallet` — только по нему. Суммы даются в валюте кошелька, итог —
в `currency`.

#### Scenario: Баланс на дату
- **WHEN** `date: "2026-09-30"`, а 2026-09-30 была трата
- **THEN** трата учтена в балансе

### Requirement: search_operations
`search_operations(from, to, text?, categories?, wallets?, kind?, limit?, cursor?)`
SHALL вернуть до `limit` (1..50, по умолчанию 20) операций периода по
убыванию даты из `GET /api/reports/operations` с фильтрами wallet-report и
флаг `has_more`. Строка: дата, `kind`, сумма с валютой, категория, кошелёк
(или откуда/куда у перевода), комментарий не длиннее 120 байт. `text` —
массив вариантов подстроки (1..10, каждый до 100 символов): подходит
комментарий с ЛЮБЫМ из них; варианты уходят повторённым `q`, операция под
несколькими вариантами в `total`/`count` входит один раз. Строка вместо
массива — `-32602`.

#### Scenario: Поиск по комментарию
- **WHEN** `text: ["такси"]`, период — сентябрь
- **THEN** возвращаются только операции сентября с «такси» (или «Такси») в комментарии

#### Scenario: Синонимы одним вызовом
- **WHEN** `text: ["стриж", "парикмах", "barber", "haircut"]`
- **THEN** в wallet-report уходит один запрос с четырьмя `q`, в ответе операции с любым из слов

#### Scenario: Больше, чем лимит
- **WHEN** подходящих операций 70, `limit` не задан
- **THEN** возвращаются 20 последних, `has_more: true` и `next_cursor`

### Requirement: Тип операций по умолчанию — расход
Аргумент `kind` у `search_operations` SHALL принимать `expense`, `income`,
`transfer` или `any` и по умолчанию равняться `expense`: на вопрос «сколько
потратил» модель должна получить сумму расходов, а не сальдо. `expense`,
`income` и `transfer` передаются в wallet-report как `kind`; при `any` фильтр
по типу не передаётся. Ответ эхом несёт фактический `kind`.

#### Scenario: Вопрос про траты без kind
- **WHEN** модель вызывает `search_operations(text: "такси")` без `kind`
- **THEN** в wallet-report уходит `kind=expense`, а в ответе `kind: "expense"`

#### Scenario: Все типы
- **WHEN** модель вызывает `search_operations(text: "Иван", kind: "any")`
- **THEN** в wallet-report фильтр `kind` не передаётся, в ответе `kind: "any"`

#### Scenario: Неизвестный тип
- **WHEN** `kind: "spending"`
- **THEN** ошибка JSON-RPC `-32602`, wallet-report не вызывается

### Requirement: Итог поиска от SQL
Если в wallet-report ушёл хотя бы один фильтр (всегда, кроме `kind: "any"`
без других фильтров), ответ `search_operations` SHALL содержать `total` —
сумму всех подходящих операций периода в `currency`, а не только показанных,
— и `incomplete` из `total_eq` и `total_incomplete` wallet-report.

#### Scenario: Сколько на такси
- **WHEN** 70 подходящих расходов, показано 20
- **THEN** `total` — сумма всех 70, посчитанная wallet-report

#### Scenario: Без фильтров
- **WHEN** `kind: "any"` и других фильтров нет
- **THEN** в ответе нет `total`

### Requirement: Знак итога поиска
При `kind` `expense`, `income` или `transfer` `total` SHALL быть
неотрицательным. При `kind: "any"` `total` SHALL быть сальдо — доходы минус
расходы, прямые переводы не входят, — и описание инструмента говорит об этом
явно.

#### Scenario: Расходы положительны
- **WHEN** `kind` не задан, найдены расходы на 1500
- **THEN** `total` — `"1500.00"`, а не `"-1500.00"`

#### Scenario: Сальдо по всем типам
- **WHEN** `kind: "any"`, `text: "Иван"`: расходы 3000 и доход 5000
- **THEN** `total` — `"2000.00"`, `kind: "any"`

### Requirement: Недоступный wallet-report
Если wallet-report не ответил или ответил кодом, кроме `200` и `401`,
инструмент SHALL вернуть `isError: true` с текстом «Отчёты временно
недоступны, попробуйте позже.», без частичных данных.

#### Scenario: wallet-report остановлен
- **WHEN** модель вызывает `list_wallets`, а wallet-report недоступен
- **THEN** `isError: true` с этим текстом

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

### Requirement: Страницы search_operations
При `has_more: true` ответ `search_operations` SHALL содержать `next_cursor`
— строку из `next_cursor_date` и `next_cursor_uuid` wallet-report через `|`.
Аргумент `cursor` с этим значением SHALL запрашивать следующую страницу
(`cursor_date`/`cursor_uuid` wallet-report); `total` и `count` — те же, что
на первой странице. При `has_more: false` поля `next_cursor` нет.

#### Scenario: Вторая страница
- **WHEN** первый вызов вернул `has_more: true` и `next_cursor`, второй вызов — те же аргументы и `cursor: <next_cursor>`
- **THEN** второй ответ — следующие операции, `total` и `count` как в первом

#### Scenario: Последняя страница
- **WHEN** подходящих операций меньше `limit`
- **THEN** `has_more: false`, `next_cursor` нет

### Requirement: Неверный курсор
`cursor` не той формы SHALL давать `isError: true` с текстом
`cursor неверен: передайте next_cursor из прошлого ответа без изменений.` до
обращения к wallet-report. `has_more: true` без курсора в ответе
wallet-report SHALL давать «Отчёты временно недоступны, попробуйте позже.»

#### Scenario: Курсор без страницы от wallet-report
- **WHEN** wallet-report ответил `has_more: true` без `next_cursor_date`/`next_cursor_uuid`
- **THEN** «Отчёты временно недоступны, попробуйте позже.»

#### Scenario: Испорченный курсор
- **WHEN** `cursor: "abc"`
- **THEN** `isError: true`, `cursor неверен: передайте next_cursor из прошлого ответа без изменений.`, запроса к wallet-report нет

### Requirement: Период search_operations по умолчанию
`from` и `to` у `search_operations` SHALL быть необязательными: без `to`
период заканчивается сегодня в поясе `MCP_TZ`, без `from` — начинается с
начала истории. Если `from` не задан, в ответе `period.from` SHALL быть
`null`, `period.to` — фактическая дата конца. Остальные инструменты с
периодом по-прежнему требуют `from` и `to`.

#### Scenario: Когда последний раз
- **WHEN** сегодня 2026-11-06, вызов `search_operations(text: ["стриж"])` без `from` и `to`
- **THEN** поиск идёт по всей истории по 2026-11-06 включительно, `period: {"from": null, "to": "2026-11-06"}`

#### Scenario: Только начало
- **WHEN** сегодня 2026-11-06, `from: "2026-01-01"` без `to`
- **THEN** `period: {"from": "2026-01-01", "to": "2026-11-06"}`

#### Scenario: Начало в будущем
- **WHEN** сегодня 2026-11-06, `from: "2026-12-01"` без `to`
- **THEN** `isError: true`, `Период неверен: from (2026-12-01) позже to (2026-11-06).`, wallet-report не вызывается

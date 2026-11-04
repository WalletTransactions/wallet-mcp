# wallet-tools Specification

## Purpose
Набор read-only инструментов кошелька для модели MCP-клиента: что каждый
инструмент принимает, откуда берёт данные в wallet-report и в каком виде их
возвращает, чтобы ответы на вопросы о тратах были посчитаны SQL, а не моделью.

## Requirements

### Requirement: Состав инструментов
`tools/list` SHALL возвращать ровно шесть инструментов: `list_wallets`,
`list_categories`, `spending_by_category`, `income_expense_by_month`,
`balance`, `search_operations`. У каждого есть описание на русском и
`inputSchema` (JSON Schema, `additionalProperties: false`). Инструментов,
меняющих данные, нет.

#### Scenario: Список
- **WHEN** клиент вызывает `tools/list`
- **THEN** в ответе шесть инструментов с этими именами, и ни один не создаёт, не меняет и не удаляет данные

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
`search_operations(from, to, text?, categories?, wallets?, kind?, limit?)`
SHALL вернуть до `limit` (1..50, по умолчанию 20) операций периода по
убыванию даты из `GET /api/reports/operations` с фильтрами wallet-report и
флаг `has_more`. Строка: дата, `kind`, сумма с валютой, категория, кошелёк
(или откуда/куда у перевода), комментарий не длиннее 120 байт.

#### Scenario: Поиск по комментарию
- **WHEN** `text: "такси"`, период — сентябрь
- **THEN** возвращаются только операции сентября с «такси» (или «Такси») в комментарии

#### Scenario: Больше, чем лимит
- **WHEN** подходящих операций 70, `limit` не задан
- **THEN** возвращаются 20 последних и `has_more: true`

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

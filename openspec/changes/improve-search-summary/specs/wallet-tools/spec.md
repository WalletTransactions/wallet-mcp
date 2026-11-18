## MODIFIED Requirements

### Requirement: search_operations
`search_operations(from, to, text?, exclude_text?, categories?, wallets?, kind?, limit?, cursor?)`
SHALL вернуть до `limit` (1..50, по умолчанию 20) операций периода по
убыванию даты из `GET /api/reports/operations` с фильтрами wallet-report и
флаг `has_more`. Строка: дата, `kind`, сумма с валютой, категория, кошелёк
(или откуда/куда у перевода), комментарий не длиннее 120 байт. `text` —
массив вариантов подстроки (1..10, каждый до 100 символов): подходит
комментарий с ЛЮБЫМ из них; варианты уходят повторённым `q`, операция под
несколькими вариантами в `total`/`count` входит один раз. `exclude_text` —
массив вариантов подстроки (1..10, каждый до 100 символов) с тем же
сопоставлением, но обратным смыслом: операция, чей комментарий содержит ЛЮБОЙ
из вариантов, не попадает ни в страницу, ни в `total`, `count` и `summary`;
варианты уходят повторённым `exclude_q`. Операция с пустым комментарием
`exclude_text` не исключается. Строка вместо массива в `text` или
`exclude_text` — `-32602`.

#### Scenario: Поиск по комментарию
- **WHEN** `text: ["такси"]`, период — сентябрь
- **THEN** возвращаются только операции сентября с «такси» (или «Такси») в комментарии

#### Scenario: Синонимы одним вызовом
- **WHEN** `text: ["стриж", "парикмах", "barber", "haircut"]`
- **THEN** в wallet-report уходит один запрос с четырьмя `q`, в ответе операции с любым из слов

#### Scenario: Больше, чем лимит
- **WHEN** подходящих операций 70, `limit` не задан
- **THEN** возвращаются 20 последних, `has_more: true` и `next_cursor`

#### Scenario: Исключение ложного срабатывания
- **WHEN** `text: ["стриж"]`, `exclude_text: ["машинк"]`, в периоде есть «Стрижка» и «Машинка для стрижки волос»
- **THEN** в wallet-report уходят `q=стриж` и `exclude_q=машинк`, «Машинка для стрижки волос» нет ни в `operations`, ни в `total`, ни в `count`

#### Scenario: exclude_text без text
- **WHEN** `exclude_text: ["перевод"]`, `text` не задан
- **THEN** возвращаются все операции периода, кроме тех, где в комментарии есть «перевод»; `total` — по оставшимся

#### Scenario: exclude_text строкой
- **WHEN** `exclude_text: "машинка"`
- **THEN** ошибка JSON-RPC `-32602`, wallet-report не вызывается

#### Scenario: Слишком много вариантов исключения
- **WHEN** `exclude_text` содержит 11 вариантов
- **THEN** ошибка JSON-RPC `-32602`, wallet-report не вызывается

## ADDED Requirements

### Requirement: Сводка поиска
Если ответ `search_operations` содержит `total` (то есть в wallet-report ушёл
хотя бы один фильтр), он SHALL содержать `summary` — сводку по ВСЕМ подходящим
операциям периода, а не только по показанным: `first_date` и `last_date`
(ГГГГ-ММ-ДД самой ранней и самой поздней подходящей операции), `by_category` —
массив `{category, count, total}` по убыванию `total` (не более 20 строк; если
категорий больше, добавляется `by_category_truncated: true`), `total` в `currency`
ответа, и `avg_interval_days` — среднее число дней между соседними по дате
подходящими операциями, `(last_date − first_date) / (count − 1)` с одним знаком
после запятой. Даты, `by_category` и `total` в `by_category` берутся из ответа
wallet-report; `avg_interval_days` — единственное значение, которое сервис
вычисляет сам, и только по датам, не по суммам. При `count` меньше 2
`avg_interval_days` SHALL отсутствовать; при `count` равном 0 — `summary`
SHALL быть без `first_date`, `last_date` и `avg_interval_days`, с пустым
`by_category`. Без фильтров (`kind: "any"` и ничего больше) `summary` нет.

#### Scenario: Когда последний раз и сколько в среднем между
- **WHEN** `text: ["стриж"]`, 3 подходящие операции, самая ранняя 2026-03-04, самая поздняя 2026-11-06 (247 дней между ними)
- **THEN** `summary.first_date` — `"2026-03-04"`, `summary.last_date` — `"2026-11-06"`, `summary.avg_interval_days` — `"123.5"`

#### Scenario: Сводка шире страницы
- **WHEN** подходящих операций 70, показано 20, самая ранняя вне страницы
- **THEN** `summary.first_date` — дата самой ранней из всех 70

#### Scenario: Разбивка по категориям
- **WHEN** подходящие операции лежат в «Personal Care» (2 шт.) и «Красота и уход» (1 шт.)
- **THEN** `summary.by_category` содержит обе категории с `count` и `total`, посчитанными wallet-report

#### Scenario: Одна операция
- **WHEN** найдена ровно одна подходящая операция
- **THEN** в `summary` есть `first_date` и `last_date` (одинаковые), `avg_interval_days` нет

#### Scenario: Ничего не найдено
- **WHEN** подходящих операций нет
- **THEN** `count: 0`, `summary.by_category` — пустой массив, `first_date`, `last_date` и `avg_interval_days` нет

#### Scenario: Без фильтров
- **WHEN** `kind: "any"` и других фильтров нет
- **THEN** в ответе нет ни `total`, ни `summary`

### Requirement: Совпадения вне фильтра категорий
Если в `search_operations` заданы и `text`, и `categories`, ответ SHALL
содержать `other_matches` — массив `{category, count, total}` по категориям,
которых нет в `categories`, где комментарий операций периода тоже совпал с
`text` (с учётом `exclude_text`, `wallets` и `kind`). Значения считает
wallet-report. Если таких категорий нет, `other_matches` SHALL быть пустым
массивом, а не отсутствовать: пустой массив — «проверено, ничего нет».
Без `text` или без `categories` `other_matches` в ответе нет.

#### Scenario: Стрижка записана в другой категории
- **WHEN** `text: ["стриж"]`, `categories: ["Personal Care"]`, ещё одна «Стрижка» 15 € лежит в «Красота и уход»
- **THEN** `other_matches` — `[{"category": "Красота и уход", "count": 1, "total": "15.00"}]`, а `total` и `summary` относятся только к «Personal Care»

#### Scenario: Вне фильтра ничего нет
- **WHEN** `text: ["стриж"]`, `categories: ["Personal Care"]`, других категорий с совпадением нет
- **THEN** `other_matches` — `[]`

#### Scenario: Без categories
- **WHEN** `text: ["стриж"]`, `categories` не задан
- **THEN** в ответе нет `other_matches`, разбивку по категориям модель берёт из `summary.by_category`

### Requirement: Устаревший wallet-report без сводки
Если wallet-report не вернул `summary` или `other_matches`, на которые сервис
рассчитывал (старая версия), `search_operations` SHALL ответить как раньше —
страница, `total`, `count`, `incomplete` — без `summary` и `other_matches`, а не
`isError`.

#### Scenario: Старый wallet-report
- **WHEN** wallet-report отвечает `200` без `summary`
- **THEN** ответ содержит `operations` и `total`, но не содержит `summary`, ошибки нет

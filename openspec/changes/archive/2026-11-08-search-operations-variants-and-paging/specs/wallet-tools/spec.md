# Spec Delta

## MODIFIED Requirements

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

## ADDED Requirements

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

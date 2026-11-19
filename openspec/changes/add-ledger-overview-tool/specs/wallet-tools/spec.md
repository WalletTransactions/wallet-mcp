## MODIFIED Requirements

### Requirement: Состав инструментов
`tools/list` SHALL возвращать восемь инструментов кошелька: `ledger_overview`,
`list_wallets`, `list_categories`, `spending_by_category`,
`spending_by_category_by_month`, `income_expense_by_month`, `balance`,
`search_operations` — и, только если задан `WALLET_DOCS_URL`, девятый: `ask_docs`
(спека `docs-lookup`). У каждого есть описание на русском и `inputSchema`
(JSON Schema, `additionalProperties: false`). Инструментов, меняющих данные, нет;
`ask_docs` данных кошелька не читает.

#### Scenario: Список
- **WHEN** `WALLET_DOCS_URL` пуст, клиент вызывает `tools/list`
- **THEN** в ответе восемь инструментов с этими именами, и ни один не создаёт, не меняет и не удаляет данные

#### Scenario: Список со справкой
- **WHEN** `WALLET_DOCS_URL` задан, клиент вызывает `tools/list`
- **THEN** в ответе девять инструментов: восемь прежних и `ask_docs`, ни один не меняет данные

## ADDED Requirements

### Requirement: ledger_overview
`ledger_overview()` без аргументов SHALL вернуть, что есть в леджере и какое
сегодня число, чтобы модель не просила период у пользователя. Ответ содержит:
`today` (`ГГГГ-ММ-ДД` в поясе сервиса), `weekday` (по-русски, строчными),
`data_period` `{"from","to"}` — от даты первой операции до сегодня, но не длиннее
36 месяцев (при более длинной истории `from` сдвигается так, что период
укладывается в 36 месяцев и начинается с первого числа месяца), `operations_count`
(число живых операций за всё время), `wallets` (`name`, `currency` живых
неархивных кошельков в порядке справочника), `currencies` (ISO-коды без повторов),
`default_currency` (валюта первого кошелька) и `hint`. Источники —
`GET /api/reports/data-range` и `GET /api/reports/accounts` wallet-report; сервис
сам операций не читает и ничего не суммирует.

Если операций нет, `data_period` равен `null`, `operations_count` — `0`, а `hint`
— «Операций пока нет — заведите их в приложении.». Иначе `hint` — «Если
пользователь не назвал период, бери data_period. Суммы и средние бери из
инструментов, сам не считай.».

#### Scenario: Данные за год
- **WHEN** первая операция 2025-12-01, сегодня 2026-11-19, 475 операций, один кошелёк «Карта Visa» в EUR
- **THEN** `data_period` — `{"from":"2025-12-01","to":"2026-11-19"}`, `operations_count` — `475`, `default_currency` — `EUR`, `hint` содержит «бери data_period»

#### Scenario: Длинная история
- **WHEN** первая операция 2019-03-14, сегодня 2026-11-19
- **THEN** `data_period.from` — `2023-12-01`, `data_period.to` — `2026-11-19`, период не длиннее 36 месяцев

#### Scenario: Пустой леджер
- **WHEN** в леджере нет операций
- **THEN** `data_period` — `null`, `operations_count` — `0`, `hint` — «Операций пока нет — заведите их в приложении.», `isError` не выставляется

#### Scenario: Нет доступа к данным
- **WHEN** wallet-report не отвечает или возвращает ошибку
- **THEN** `isError: true` с текстом ошибки, как у остальных инструментов

### Requirement: Подсказка про период в описаниях
Описание каждого инструмента с периодом (`spending_by_category`,
`spending_by_category_by_month`, `income_expense_by_month`, `search_operations`)
SHALL начинаться с фразы «Период не назван — сначала вызови ledger_overview.».
Описание `ledger_overview` SHALL начинаться с фразы «Вызови первым, если период
в вопросе не назван.».

#### Scenario: Описание инструмента с периодом
- **WHEN** клиент вызывает `tools/list`
- **THEN** `description` у `spending_by_category_by_month` начинается с «Период не назван — сначала вызови ledger_overview.»

#### Scenario: Описание ledger_overview
- **WHEN** клиент вызывает `tools/list`
- **THEN** `description` у `ledger_overview` начинается с «Вызови первым, если период в вопросе не назван.»

## MODIFIED Requirements

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

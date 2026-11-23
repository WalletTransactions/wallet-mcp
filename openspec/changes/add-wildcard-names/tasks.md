## 1. "*" означает все

- [x] 1.1 В `tools::call` (`src/tools.cpp`) после `validate` снимать `wallets`/`categories`/`exclude_categories` с `"*"` в массиве и `wallet == "*"`; инструменты получают уже очищенные аргументы. Проверка: тесты в `tests/test_tools.cpp` — `search_operations` с `wallets: ["*"]` без `wallet_uuid` в запросе, `wallets: ["*", "Наличные"]` тоже, `spending_by_category` с `categories: ["*"]` равен вызову без `categories`, `balance` с `wallet: "*"` по всем кошелькам, `categories: ["*"]` + `exclude_categories` не ошибка; `ctest` зелёный
- [x] 1.2 Описания `wallets` и `categories` в `tools/list`: «не передавай, чтобы взять все». Проверка: тест на `tools/list` в `tests/test_mcp.cpp` находит фразу у `search_operations.wallets` и `spending_by_category.categories`

## 2. outside_wallets

- [x] 2.1 В `search_operations`: при `wallets`, без курсора и `total_count` 0 — второй `GET /api/reports/operations` с теми же параметрами без `wallet_uuid`, `limit=1`, без `summary`; `outside_wallets: {count, total}`; нет итога или `ReportsDown` — поля нет. Проверка: тесты по сценариям спеки (в другом кошельке, нигде нет, нашлось — второго запроса нет, без `wallets`, отказ второго запроса, курсор); `ctest` зелёный
- [x] 2.2 Описание `search_operations` — фраза про `outside_wallets`; README — строка таблицы и абзац. Проверка: тест на `tools/list` находит `outside_wallets` в описании

## 3. Проверка на живом стеке

- [ ] 3.1 Собрать образ (один C++-сервис, `BUILD_JOBS=3`, `--memory=3g`), выложить rc на test. Проверка: в чате test «покажи все операции за нянь в ноябре» — ответ перечисляет 7 операций с Cash

# Tasks

## 1. Варианты текста

- [x] 1.1 Схема: `text` — `array` строк, `minItems 1`, `maxItems 10`, `items.maxLength 100`; валидатор проверяет элементы массива строк по `items` (D1). Проверка: `ctest` — `mcp_schema_validation`: 101 символ в элементе, строка вместо массива, пустой массив, 11 элементов, число в массиве → `-32602`; 10 элементов и 100 кириллических букв — проходят — выполнено 2026-11-06
- [x] 1.2 `search_operations`: варианты (trim, без пустых и повторов) — повторённым `q`; фильтр по тексту есть, если остался хоть один вариант (D2). Проверка: `ctest` — `tools_search_text_variants_as_repeated_q` (три `q` из пяти значений, итог из ответа, только пустые — без `q`); прежние тесты с `text: ["…"]` зелёные — выполнено 2026-11-06

## 2. Страницы

- [x] 2.1 `cursor` в схеме (`string`, до 80 символов); разбор «дата|uuid», проверка формы до запроса; `cursor_date`/`cursor_uuid` в запрос; `next_cursor` при `has_more`; `has_more` без курсора — недоступно (D3). Проверка: `ctest` — `tools_search_cursor_pages` (две страницы, курсор в запросе, `total`/`count` одинаковые, на последней нет `next_cursor`), `tools_search_bad_cursor_is_tool_error` (6 испорченных курсоров, запросов 0), `tools_search_has_more_without_cursor_is_unavailable`, `tools_search_defaults_to_expense` проверяет `next_cursor` — выполнено 2026-11-06: `docker build --target test` — все тесты зелёные

## 3. Документация

- [x] 3.1 Описание инструмента в реестре (варианты одним вызовом, `next_cursor` → `cursor`); README — строка таблицы и абзац `search_operations`. Проверка: таблица совпадает с `inputSchema` в `build_registry()` — выполнено 2026-11-06 (сверено вручную)

## 4. Живой стек

- [x] 4.1 После выкладки wallet-api 000032, wallet-report `search-text-variants` и этого change на test — через гейтвей `/api/mcp/`: `search_operations(2020-01-01, today, text: ["стриж", "парикмах", "barber", "haircut"])` даёт `count` = сумме `count` по отдельным вызовам минус пересечения; `limit: 5` → `next_cursor` → вторая страница без повторов. Проверка: вывод curl приложен к тест-задаче — проверено 2026-11-06 на test (wallet-api 0.3.0-rc.3, wallet-report 0.2.0-rc.2, wallet-mcp 0.3.0-rc.2) через гейтвей `/api/mcp/`, 2020-01-01…2026-11-06: по отдельности «стриж» 21 (257.10 EUR), «парикмах» 1 (15.00), «barber» 0, «haircut» 0; все четыре одним вызовом — `count` 22, `total` 272.10 = объединение без пересечений; `limit: 5` → 5 страниц (5+5+5+5+2) по `next_cursor`, 22 строки без повторов, тот же набор; битый `cursor` — `isError` «cursor неверен: передайте next_cursor из прошлого ответа без изменений.»
- [ ] 4.2 Живой прогон из LM Studio: «когда я последний раз стригся?». Проверка: в логе wallet-mcp один вызов `search_operations` с массивом вариантов в `text` — 2026-11-06 на test (LM Studio `wallet-parse`, веб-чат): не пройдено — модель сделала один вызов, но с одним вариантом `search_operations(text=["стриж"])`, синонимов не подставила; ответ — 07.09.2026 («Машинка для стрижки волос», Electronics) вместо 04.03.2026. Качество модели/промпта wallet-ai, не wallet-mcp Повтор с промптом wallet-ai `chat-v3` (wallet-ai#13, 0.2.0-rc.3) — Тест: [wallet-ai#15](https://github.com/WalletTransactions/wallet-ai/issues/15)

## Workflow follow-up

- Выкатывается после wallet-api 000032 и wallet-report `search-text-variants`; архивировать вместе с ним.

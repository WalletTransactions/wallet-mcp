# Tasks

## 1. Период по умолчанию

- [x] 1.1 Схема: `from`/`to` у `search_operations` не в `required`, описания «Не задан — с начала истории / сегодня»; у остальных инструментов `required` прежний. Проверка: `ctest` — `mcp_schema_validation`: `search_operations` без аргументов и с одним `to` проходит, `income_expense_by_month` без `to` — `-32602` — выполнено 2026-11-06
- [x] 1.2 `search_period`: `to` — `ctx.today`, `from` — `1900-01-01`, проверка порядка; эхо `period.from: null` без `from` (D1–D3). Проверка: `ctest` — `tools_search_default_period` (запрос `from=1900-01-01 00:00:00`, `to=<сегодня> 23:59:59`, эхо; только `from`; только `to`), `tools_search_default_period_errors` (`from` в будущем → «Период неверен…», битая `to`, запросов 0) — выполнено 2026-11-06: `docker build --target test` — все тесты зелёные
- [x] 1.3 Описание инструмента («для «когда последний раз…» период не задавай») и README (строка таблицы, абзац). Проверка: таблица совпадает с `inputSchema` — выполнено 2026-11-06 (сверено вручную)

## 2. Живой стек

- [x] 2.1 После выкладки на test — через гейтвей `/api/mcp/`: `search_operations(text: ["стриж", "парикмах"])` без периода находит «Стрижка» 2026-03-04, `period.from: null`, `period.to` — сегодня. Проверка: вывод curl приложен к тест-задаче — проверено 2026-11-06 на test (wallet-mcp 0.3.0-rc.3) через гейтвей `/api/mcp/`: `search_operations(text: ["стриж", "парикмах"])` без периода — `period` `{"from": null, "to": "2026-11-06"}`, `count` 26, `total` 305.10 EUR, «Стрижка» 2026-03-04 в выдаче (вторая после «Машинка для стрижки волос» 2026-09-07)
- [ ] 2.2 LM Studio, чат без системного промпта wallet-parse: «когда я последний раз стригся?» — модель вызывает `search_operations` без `from`/`to` и называет 04.03.2026 (или 11.06.2026, если найдёт по категории). Проверка: лог wallet-mcp + ответ модели — 2026-11-06 на test (wallet-mcp 0.3.0-rc.3, веб-чат, промпт chat wallet-ai): вызов верный — `search_operations(text=["стриж"])` без `from`/`to`, но ответ не пройден: «последний раз 7 сентября 2026 … Personal Care … 62.10 EUR» — это покупка машинки (Electronics), не стрижка; ожидалось 04.03.2026. Качество модели, не wallet-mcp Повтор с промптом wallet-ai `chat-v3` (wallet-ai#13, 0.2.0-rc.3) — Тест: [wallet-ai#15](https://github.com/WalletTransactions/wallet-ai/issues/15)

## Workflow follow-up

- Правила из `instructions` (не считать суммы самой, переспрашивать при неоднозначном имени, называть period и currency) перенести в описания инструментов — LM Studio `instructions` не видит. Отдельный change.

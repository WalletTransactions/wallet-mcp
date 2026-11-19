# Tasks

Порядок выкатки: сначала wallet-report (раздел 1), потом wallet-mcp (разделы 2–4).
Сборка Docker по одному C++-сервису за раз.

## 1. wallet-report: эндпоинт data-range

- [x] 1.1 Завести в wallet-report отдельный change с эндпоинтом `GET /api/reports/data-range` (`first_date`, `last_date`, `operations_count`, пустой леджер — `null`/`0`) и записью в `openapi.yaml`; проверка: `openspec validate` change в wallet-report проходит
- [x] 1.2 Реализовать эндпоинт одним запросом по живым операциям леджера пользователя; проверка: юнит/интеграционный тест на леджер с операциями и на пустой леджер
- [x] 1.3 Выкатить wallet-report на test; проверка: `GET /api/reports/data-range` с токеном демо-аккаунта возвращает `first_date` `2025-12-01`, `operations_count` `475` (0.3.3-rc.1, 2026-11-19; на демо-аккаунте — через `ledger_overview`, см. 4.2)

## 2. wallet-mcp: инструмент ledger_overview

- [x] 2.1 Добавить клиент `data-range` рядом с клиентами wallet-report; проверка: ctest — разбор ответа с датами и с `null` (отдельного клиента не понадобилось: общий `reports.get` через `Call::fetch`, разбор — в `ledger_overview`, покрыт сценариями с датами и с `null`)
- [x] 2.2 Реализовать `ledger_overview` (`today`, `weekday`, `data_period` с зажимом до 36 месяцев с первого числа месяца, `operations_count`, `wallets`, `currencies`, `default_currency`, `hint`) и пустой леджер; проверка: ctest — сценарии «Данные за год», «Длинная история», «Пустой леджер»
- [x] 2.3 Ошибка апстрима превращается в `isError: true`; проверка: ctest со сбоем `data-range` и со сбоем `accounts`
- [x] 2.4 Зарегистрировать инструмент в `tools/list` (восемь, с `ask_docs` — девять); проверка: ctest на состав списка в обоих режимах `WALLET_DOCS_URL`

## 3. Описания и instructions

- [x] 3.1 Начать описания `spending_by_category`, `spending_by_category_by_month`, `income_expense_by_month`, `search_operations` с «Период не назван — сначала вызови ledger_overview.», описание `ledger_overview` — с «Вызови первым, если период в вопросе не назван.»; проверка: ctest сверяет начало каждого `description`
- [x] 3.2 Добавить в `instructions` строку «Если период в вопросе не назван, не спрашивай его у пользователя — вызови ledger_overview.»; проверка: ctest на `initialize`

## 4. Документация и проверка на живом стеке

- [x] 4.1 Добавить `ledger_overview` в таблицу инструментов `README.md`; проверка: README описывает восемь инструментов и их аргументы
- [x] 4.2 Выкатить wallet-mcp на test и вызвать `ledger_overview` через гейтвей токеном демо-аккаунта; проверка: `data_period` `2025-12-01..2026-11-19`, `operations_count` `475`, `default_currency` `EUR` (0.3.3-rc.1, 2026-11-19; вызов из новой сессии Claude Code на MCP `wallet-test`: `data_period` `2025-12-01..2026-11-19`, 475, EUR; на QA-аккаунте владельца — зажим до `2023-12-01`)
- [x] 4.3 В LM Studio (модель с вызовом инструментов) и в Claude Code задать «Сколько я тратил на кафе летом и сколько зимой?»; проверка: обе вызывают `ledger_overview`, не переспрашивают период, цифры лета 730,67 € и зимы 143,01 € совпадают с `spending_by_category_by_month` с `months_of_year` (Claude Code — выполнено 2026-11-19: `ledger_overview` вызван первым, период не переспрошен, 730,67 € / 143,01 €, средние 243,56 / 47,67; LM Studio — частично: `wallet-qwen` (Qwen3-8B, 2 прогона без `/no_think`) оба раза вызвала `ledger_overview` первым, затем `spending_by_category_by_month`, период не переспросила; цифры её ответа не сверены. `wallet-parse` (gemma-4-e4b) инструмент не вызывает и переспрашивает период — риск «модель не вызовет `ledger_overview`» из design сработал для неё, решается выбором модели, а не сервером)

## Workflow follow-up

- Архивировать change (`openspec archive add-ledger-overview-tool`) после слияния PR и выкатки на test.
- После выкатки пересъёмка кадров LM Studio и Claude для change `add-showcase-page` в wallet-site (задача 3.4).

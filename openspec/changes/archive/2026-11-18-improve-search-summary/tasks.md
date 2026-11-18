# Tasks

## 1. wallet-report: сопутствующий change (делается и выкатывается первым)

- [x] 1.1 Создать в `wallet-report` change `operations-search-summary` (`/opsx:propose`): параметры `exclude_q` и `summary=true` у `GET /api/reports/operations`, поля `summary` и `other_matches` в `OperationsReport` — по D1 и D3 этого change. Проверка: `openspec validate operations-search-summary` в wallet-report.
- [x] 1.2 SQL: `exclude_q` (НЕ по любому варианту, `ё` = `е`, `%`/`_` буквально, пустой комментарий не исключается), `MIN/MAX(date)`, `GROUP BY` категории по тому же набору, что `total_eq`; `other_matches` — тот же `q` без `category_uuid`. Проверка: юнит/интеграционные тесты wallet-report на сценарии «Машинка для стрижки», «стрижка в другой категории», пустой результат.
- [x] 1.3 `summary` присутствует при `exclude_q` даже при `count: 0` (признак поддержки, D5). Проверка: тест wallet-report.
- [ ] 1.4 Замерить время запроса с `summary=true` на тестовой базе (год данных, `q` из 4 вариантов). Проверка: цифра в описании PR wallet-report; если заметно хуже базового запроса — вернуться к D1. **Не выполнено при архивации:** замер не делали.
- [x] 1.5 Обновить `openapi.yaml` wallet-report, выкатить на test (rc-тег). Проверка: `curl` к `/api/reports/operations?...&summary=true` на test возвращает `summary`.

## 2. wallet-mcp: аргумент exclude_text

- [x] 2.1 В схему `search_operations` (`src/tools.cpp`, рядом с `text`) добавить `exclude_text`: массив строк, `maxItems` 10, `maxLength` 100. Проверка: `tools/list` в `tests/test_mcp.cpp` содержит новую схему.
- [x] 2.2 Общий разбор вариантов для `text` и `exclude_text` (trim, пустые и повторы отбросить, строка вместо массива и более 10 вариантов — `-32602`). Проверка: `ctest` — тесты `-32602` для `exclude_text: "x"` и 11 вариантов, wallet-report не вызывается.
- [x] 2.3 Передавать варианты повторённым `exclude_q`, `summary=true` — при любом фильтре; `exclude_text` считать фильтром (в ответе есть `total`). Проверка: `ctest` с подставным wallet-report — в запросе ровно ожидаемые `q`, `exclude_q`, `summary`.
- [x] 2.4 Если задан `exclude_text`, а в ответе wallet-report нет `summary` — `ReportsDown()` (D5). Проверка: `ctest` — старый wallet-report даёт «Отчёты временно недоступны, попробуйте позже.», а не неотфильтрованный итог.

## 3. wallet-mcp: summary и other_matches

- [x] 3.1 Собрать `summary`: `first_date`, `last_date`, `by_category` (имена вместо uuid, суммы через `amount()` с точностью валюты, top-20 и `by_category_truncated`), без блока при отсутствии `summary` в ответе wallet-report. Проверка: `ctest` на сценарии спеки «Разбивка по категориям», «Ничего не найдено», «Без фильтров», «Старый wallet-report».
- [x] 3.2 `avg_interval_days`: `(last − first) / (count − 1)` по датам, один знак после запятой, только при `count ≥ 2` (D2). Проверка: `ctest` — 3 операции 2026-03-04 … 2026-11-06 дают `"123.5"`; одна операция — поля нет.
- [x] 3.3 `other_matches`: только при `text` + `categories`, `[]` при отсутствии совпадений, имена категорий вместо uuid. Проверка: `ctest` на сценарии «Стрижка записана в другой категории», «Вне фильтра ничего нет», «Без categories».
- [x] 3.4 Описание `search_operations` в `tools/list` (D6). Проверка: `ctest` — в описании упомянуты `exclude_text`, `summary`, `other_matches`.

## 4. Проверка на живом стеке и документация

- [ ] 4.1 `scripts/smoke.sh`: добавить вызов `search_operations` с `text` + `exclude_text` + `categories`. Проверка: `scripts/smoke.sh` на test зелёный. **Не выполнено при архивации:** вызов добавлен в scripts/smoke.sh, но сам скрипт не запускали (проверено прямым JSON-RPC).
- [x] 4.2 README.md (таблица инструментов): `exclude_text`, `summary`, `other_matches`. Проверка: строка `search_operations` в таблице содержит новые аргументы и поля.
- [x] 4.3 Выкатить wallet-mcp на test (после wallet-report, по одному C++-сервису за раз). Проверка: через MCP-токен `wallet-test` вызвать `search_operations(text: ["стриж","парикмах","barber","haircut","барбер"], exclude_text: ["машинк"], from: "2026-01-01", to: "2026-11-18")` — `total` 33.00, `summary.last_date` 2026-11-06, `other_matches` не содержит «Электроники».
- [ ] 4.4 Проверить в чате (LM Studio), что на «сколько на стрижку и когда последний раз» модель отвечает одним вызовом. Проверка: лог `~/.lmstudio/server-logs` — один `search_operations`. **Не выполнено при архивации:** проверка в LM Studio не выполнена.

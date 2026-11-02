# service-endpoints Specification

## Purpose
Служебная часть wallet-mcp, общая с другими C++-сервисами проекта: запуск по
окружению, liveness, версия, метрики, самоописание API и логи без денежных
данных пользователя.

## Requirements

### Requirement: Конфигурация при старте
Сервис SHALL читать `.env` из рабочей директории (переменные окружения
процесса приоритетнее) и требовать `AUTH_SERVICE_URL` и `WALLET_REPORT_URL`.
Если любая из них пуста, он пишет в лог `<KEY> must be set — <подсказка>
(см. README.md)` и завершается с кодом `1`. `PORT` по умолчанию `8088`,
`MCP_TZ` (пояс IANA для «сегодня») по умолчанию `UTC`.

#### Scenario: Нет адреса wallet-report
- **WHEN** сервис запускается без `WALLET_REPORT_URL`
- **THEN** в логе `WALLET_REPORT_URL must be set — …`, процесс завершается с кодом 1, порт не открывается

#### Scenario: Неизвестный пояс
- **WHEN** `MCP_TZ=Mars/Olympus`
- **THEN** процесс пишет ошибку и завершается с кодом 1

### Requirement: Liveness
`GET /healthz` SHALL без аутентификации отвечать `200` `{"status":"ok"}`, не
проверяя google-auth и wallet-report.

#### Scenario: Проба compose
- **WHEN** healthcheck вызывает `/healthz` при остановленном wallet-report
- **THEN** ответ `200`, запрос не пишется в access-лог

### Requirement: Версия билда
`GET /version` SHALL без аутентификации возвращать `service` (`wallet-mcp`),
`version` (из файла `VERSION`) и `git_commit` (короткий хэш, `unknown` при
сборке без git). Те же значения отдаются в `build_info` метрик и в строке
лога при старте.

#### Scenario: Сборка без git
- **WHEN** образ собран без `GIT_COMMIT`
- **THEN** `git_commit` равен `unknown`

### Requirement: Метрики Prometheus
`GET /metrics` SHALL отдавать гистограмму HTTP-запросов, счётчик вызовов
инструментов `mcp_tool_calls_total{tool,result}` (`ok`/`tool_error`/`unavailable`),
длительность вызова `mcp_tool_duration_seconds{tool}`, обмены токена
`mcp_token_exchange_total{result}` (`ok`/`invalid`/`unavailable`) и попадания
кеша `mcp_token_cache_total{result}` (`hit`/`miss`).

#### Scenario: Вопрос про траты
- **WHEN** модель один раз вызывает `spending_by_category` с новым токеном
- **THEN** растут `mcp_tool_calls_total{tool="spending_by_category",result="ok"}`, `mcp_token_exchange_total{result="ok"}` и `mcp_token_cache_total{result="miss"}`

### Requirement: Самоописание API
`GET /openapi.yaml` SHALL отдавать файл спецификации HTTP-интерфейса сервиса,
`GET /docs` — Swagger UI из вендоренного бандла без обращения к CDN.

#### Scenario: Документация
- **WHEN** браузер открывает `/docs`
- **THEN** Swagger UI загружается только с ресурсов самого сервиса

### Requirement: Логи без денежных данных
Сервис SHALL писать JSON-логи с `trace_id` из `X-Trace-Id` (сам его не
порождает). Для вызова инструмента в лог пишутся `user_id`, имя инструмента,
исход и `duration_ms`. Аргументы, суммы, имена счетов, комментарии, тела
ответов и токены (MCP-токен, JWT, их хэши) в лог SHALL NOT попадать.

#### Scenario: Поиск по комментарию
- **WHEN** модель ищет `text: "няня Маша"`
- **THEN** в логе есть `tool: "search_operations"` и исход, но нет «няня Маша» и найденных сумм

#### Scenario: Отказ по токену
- **WHEN** запрос пришёл с отозванным токеном
- **THEN** в лог пишется событие `mcp_auth_rejected` без токена и его хэша

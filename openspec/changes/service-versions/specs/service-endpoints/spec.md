# Spec Delta

## MODIFIED Requirements

### Requirement: Версия билда
`GET /version` SHALL без аутентификации возвращать `service` (`wallet-mcp`),
`version` (из файла `VERSION`), `build` (тег образа из `APP_BUILD`, без него
`<VERSION>-dev`), `git_commit` (короткий хэш, `unknown` при сборке без git) и
`started_at` (время запуска процесса, ISO-8601 UTC). `service`, `version`,
`build` и `git_commit` отдаются в `build_info` метрик и в строке лога при старте,
время запуска — в `process_start_time_seconds`.

#### Scenario: Сборка без git
- **WHEN** образ собран без `GIT_COMMIT`
- **THEN** `git_commit` равен `unknown`

#### Scenario: Сборка без тега
- **WHEN** образ собран без `APP_BUILD`
- **THEN** `build` равен `<VERSION>-dev`

#### Scenario: Время запуска
- **WHEN** `/version` вызван дважды без перезапуска, а затем после перезапуска контейнера
- **THEN** первые два `started_at` равны, третий — время нового запуска

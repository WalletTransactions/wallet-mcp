# Spec Delta

## ADDED Requirements

### Requirement: Период search_operations по умолчанию
`from` и `to` у `search_operations` SHALL быть необязательными: без `to`
период заканчивается сегодня в поясе `MCP_TZ`, без `from` — начинается с
начала истории. Если `from` не задан, в ответе `period.from` SHALL быть
`null`, `period.to` — фактическая дата конца. Остальные инструменты с
периодом по-прежнему требуют `from` и `to`.

#### Scenario: Когда последний раз
- **WHEN** сегодня 2026-11-06, вызов `search_operations(text: ["стриж"])` без `from` и `to`
- **THEN** поиск идёт по всей истории по 2026-11-06 включительно, `period: {"from": null, "to": "2026-11-06"}`

#### Scenario: Только начало
- **WHEN** сегодня 2026-11-06, `from: "2026-01-01"` без `to`
- **THEN** `period: {"from": "2026-01-01", "to": "2026-11-06"}`

#### Scenario: Начало в будущем
- **WHEN** сегодня 2026-11-06, `from: "2026-12-01"` без `to`
- **THEN** `isError: true`, `Период неверен: from (2026-12-01) позже to (2026-11-06).`, wallet-report не вызывается

## MODIFIED Requirements

### Requirement: Конфигурация при старте
Сервис SHALL читать `.env` из рабочей директории (переменные окружения
процесса приоритетнее) и требовать `AUTH_SERVICE_URL` и `WALLET_REPORT_URL`.
Если любая из них пуста, он пишет в лог `<KEY> must be set — <подсказка>
(см. README.md)` и завершается с кодом `1`. `PORT` по умолчанию `8088`,
`MCP_TZ` (пояс IANA для «сегодня») по умолчанию `UTC`. `AUTH_EXCHANGE_SECRET`
необязателен: без него приём JWT выключен, и токены без префикса `wmcp_`
получают `401`. Ключи для проверки JWT сервис берёт из
`AUTH_SERVICE_URL` + `/.well-known/jwks.json` при первой проверке, а не на
старте.

#### Scenario: Нет адреса wallet-report
- **WHEN** сервис запускается без `WALLET_REPORT_URL`
- **THEN** в логе `WALLET_REPORT_URL must be set — …`, процесс завершается с кодом 1, порт не открывается

#### Scenario: Неизвестный пояс
- **WHEN** `MCP_TZ=Mars/Olympus`
- **THEN** процесс пишет ошибку и завершается с кодом 1

#### Scenario: Без секрета обмена
- **WHEN** сервис запущен без `AUTH_EXCHANGE_SECRET`
- **THEN** запросы с `wmcp_…` обслуживаются, запросы с JWT для wallet-mcp получают `401`, в логе старта предупреждение `JWT access disabled (AUTH_EXCHANGE_SECRET empty)`

#### Scenario: google-auth недоступен на старте
- **WHEN** `AUTH_EXCHANGE_SECRET` задан, а google-auth на старте не отвечает
- **THEN** сервис стартует и открывает порт, ключи загружаются при первом запросе с JWT

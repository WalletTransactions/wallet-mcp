## MODIFIED Requirements

### Requirement: Токен в каждом запросе
Каждый запрос к `/api/mcp/`, включая `initialize` и уведомления, SHALL нести
`Authorization: Bearer <токен>`, где токен — персональный `wmcp_…` или JWT
для wallet-mcp (см. «Проверка JWT для wallet-mcp»). Проверка выполняется до
разбора JSON-RPC. Cookie и другие источники токена сервис не читает.

#### Scenario: Без заголовка
- **WHEN** `POST /api/mcp/` с `initialize` без `Authorization`
- **THEN** ответ `401`, google-auth и wallet-report не вызываются

#### Scenario: JWT вместо MCP-токена
- **WHEN** `Authorization: Bearer <access-JWT веб-сессии>` (в `aud` нет `wallet-mcp`)
- **THEN** ответ `401`, токен в google-auth не отправляется

#### Scenario: JWT, выданный обменом для wallet-mcp
- **WHEN** wallet-ai присылает JWT с `aud` `["wallet-mcp"]` и `scope` `mcp:read`
- **THEN** запрос обслуживается от имени `sub` этого JWT

### Requirement: Обмен на JWT
Сервис SHALL вызывать wallet-report только JWT, полученным от google-auth для
`wallet-report`: для `wmcp_…` — через `POST /internal/mcp/token` `{"token"}`,
для JWT — через `POST /internal/token/exchange` (actor `wallet-mcp`,
`audience=wallet-report`; контракты — `openapi.yaml` google-auth).
Предъявленный клиентом токен в wallet-report не уходит.

#### Scenario: Действительный токен
- **WHEN** запрос несёт действительный `wmcp_…`, кеша для него нет
- **THEN** сервис один раз вызывает `/internal/mcp/token` и выполняет запрос с полученным `access_token`

#### Scenario: Действительный JWT
- **WHEN** запрос несёт действительный JWT для wallet-mcp, кеша для него нет
- **THEN** сервис один раз вызывает `/internal/token/exchange` с `X-Service-Secret` из `AUTH_EXCHANGE_SECRET` и выполняет запрос с полученным `access_token`, а не с входящим JWT

### Requirement: Кеш JWT
JWT для wallet-report SHALL кешироваться в памяти по SHA-256 предъявленного
токена и переиспользоваться, пока до его `exp` остаётся больше 30 с.
Предъявленный токен в памяти не хранится. При рестарте кеш пуст.

#### Scenario: Серия вызовов
- **WHEN** модель делает пять вызовов инструментов за минуту одним токеном
- **THEN** обмен в google-auth вызван один раз

#### Scenario: JWT подходит к концу
- **WHEN** до `exp` закешированного JWT осталось 20 с
- **THEN** перед вызовом wallet-report сервис заново выполняет обмен

#### Scenario: Входящий JWT истекает раньше кеша
- **WHEN** входящий JWT для wallet-mcp истёк, а JWT для wallet-report по нему ещё в кеше
- **THEN** ответ `401`: входящий JWT проверяется на каждом запросе, кеш не продлевает его

### Requirement: Отказ в доступе
Если токена нет, у него не тот формат, JWT не прошёл проверку или google-auth
отказал в обмене (`401` на `/internal/mcp/token`, `400` на
`/internal/token/exchange`), сервис SHALL ответить HTTP `401` с
`Content-Type: application/json`, телом
`{"error":"invalid_token","message":"MCP-токен недействителен или истёк — выпустите новый в веб-кабинете: Настройки → Устройства"}`
и заголовком `WWW-Authenticate: Bearer`.

#### Scenario: Отозванный токен
- **WHEN** пользователь отозвал MCP-сессию в «Моих устройствах», кеш JWT для токена истёк, и клиент делает вызов
- **THEN** ответ `401` с этим `message`

#### Scenario: Обмен JWT отклонён
- **WHEN** google-auth отвечает `400` `{"error":"invalid_grant"}` на обмен входящего JWT
- **THEN** ответ `401` с этим `message`

### Requirement: Недоступный google-auth
Если google-auth не ответил (сеть, таймаут) или ответил на обмен кодом, кроме
перечисленных в «Отказе в доступе» и `200`, сервис SHALL ответить HTTP `503`
с `{"error":"unavailable","message":"Сервис авторизации временно недоступен, попробуйте позже"}`
и не считать токен недействительным. Ответ google-auth `401`
`{"error":"invalid_client"}` на `/internal/token/exchange` (неверный
`AUTH_EXCHANGE_SECRET`) — тоже `503`: это ошибка конфигурации сервиса, а не токена.

#### Scenario: google-auth перезапускается
- **WHEN** кеша для токена нет, а google-auth не отвечает
- **THEN** ответ `503`, а следующий запрос после подъёма google-auth проходит

#### Scenario: Неверный секрет обмена
- **WHEN** `AUTH_EXCHANGE_SECRET` не совпадает с секретом `wallet-mcp` в google-auth
- **THEN** запросы с JWT получают `503`, в журнале ошибка `token_exchange_invalid_client`, запросы с `wmcp_…` работают

## ADDED Requirements

### Requirement: Проверка JWT для wallet-mcp
Токен без префикса `wmcp_` SHALL приниматься, только если это JWT RS256 с
подписью по JWKS google-auth, `iss` `google-auth`, `aud` ∋ `wallet-mcp`,
`scope` ∋ `mcp:read` и неистёкшим `exp`. Проверка локальная, без вызова
google-auth. Любое несоответствие — `401` из «Отказа в доступе».

#### Scenario: Токен для другого адресата
- **WHEN** JWT подписан google-auth, но `aud` `["wallet-report"]`
- **THEN** ответ `401`, обмен не вызывается

#### Scenario: Подделанная подпись
- **WHEN** JWT с `aud` `wallet-mcp` подписан чужим ключом
- **THEN** ответ `401`

### Requirement: Журнал вызывающего
Запись журнала о запросе к `/api/mcp/` SHALL содержать вид учётных данных
(`credential`: `pat` для `wmcp_…`, `jwt` для JWT) и для JWT цепочку actor'ов
из claim `act` (например, `wallet-ai`). Метрика `mcp_tool_calls_total` SHALL
иметь метку `credential`. Сами токены в журнал не попадают.

#### Scenario: Вызов из чата wallet-ai
- **WHEN** wallet-ai вызывает `tools/call` JWT с `act` `{"sub":"wallet-ai"}`
- **THEN** в журнале запроса `credential` `jwt`, `actor` `wallet-ai`, `user_id` из `sub`

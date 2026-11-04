# Proposal

## Why

wallet-ai будет вести чат с ИИ по данным кошелька и исполнять инструменты
через wallet-mcp (change `add-chat-agent` в wallet-ai). У wallet-ai нет
персонального токена `wmcp_…`, только JWT пользователя, а пробрасывать
чужой JWT спека MCP запрещает. Поэтому wallet-mcp должен стать обычным
resource server: принимать короткий JWT, выпущенный именно для него
(`aud` `wallet-mcp`, обмен в google-auth, change `add-token-exchange`). Это
же нужно для будущего OAuth 2.1 у внешних MCP-клиентов: там тоже придёт JWT
с `aud` `wallet-mcp`.

## What Changes

- `/api/mcp/` принимает два вида `Authorization: Bearer`:
  - `wmcp_…` — как сейчас, обмен через `/internal/mcp/token`;
  - JWT RS256 от google-auth с `aud` ∋ `wallet-mcp` и `scope` ∋ `mcp:read`,
    проверяется локально по JWKS google-auth.
- Для JWT сервис получает токен для wallet-report обменом
  `POST /internal/token/exchange` (actor `wallet-mcp`, `audience=wallet-report`).
  Входящий JWT в wallet-report не уходит.
- Кеш JWT для wallet-report работает для обоих видов. Ключ — SHA-256
  предъявленного токена. Для входящего JWT кеш не переживает его `exp`.
- Журнал запроса и метрики различают вид учётных данных (`pat`/`jwt`), для
  JWT пишется цепочка `act` (кто действует от имени пользователя).
- Новая переменная `AUTH_EXCHANGE_SECRET` (свой `X-Service-Secret` для
  обмена). Ключи JWKS берутся из того же `AUTH_SERVICE_URL`.
- **Поведение для `wmcp_…` не меняется.** JWT веб-сессии (без `wallet-mcp` в
  `aud`) по-прежнему получает `401` и в google-auth не отправляется.

## Capabilities

### New Capabilities

(нет)

### Modified Capabilities

- `mcp-access`: токен в запросе может быть JWT для wallet-mcp. Обмен и кеш
  описаны для двух видов учётных данных, добавлены проверка JWT и журнал actor'а.
- `service-endpoints`: необязательный `AUTH_EXCHANGE_SECRET` включает приём
  JWT. Без него сервис стартует и принимает только `wmcp_…`.

## Impact

- **Код:** `src/access.{hpp,cpp}` (вид учётных данных, проверка JWT, кеш),
  `src/auth_client.{hpp,cpp}` (второй метод — обмен RFC 8693),
  `src/mcp_http.cpp` (выбор пути, журнал), `src/main.cpp` (конфиг, JWKS).
  Используются вендоренные `util/jwks`, `util/jwt`, `util/jwt_claims`, правок
  в wallet-common нет.
- **Зависимость:** google-auth с `add-token-exchange` и заданным секретом
  `wallet-mcp` в `TOKEN_EXCHANGE_SECRETS`.
- **Конфиг:** `AUTH_EXCHANGE_SECRET` в `.env.example`,
  `README.md`. Значения — в wallet-infra.
- **Без правок:** wallet-gateway (`/api/mcp/` уже в обход JWT-гейта),
  wallet-report (получает обычный `reports:read` без `role`).
- **Вне объёма:** метаданные OAuth (RFC 9728), `resource_metadata` в `401`.
  Это следующий этап, для внешних клиентов.

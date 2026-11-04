# Design

## Context

- Сейчас `mcp_http` берёт `Bearer`, отсекает всё без префикса `wmcp_`
  (`looks_like_mcp_token`) и отдаёт токен в `TokenCache`. `TokenCache` по
  SHA-256 токена хранит JWT для wallet-report, а при промахе зовёт
  `AuthApi::exchange` (`/internal/mcp/token`). `UserReports` повторяет вызов
  один раз после `401` от wallet-report.
- В `third_party/wallet-common/util` уже есть `jwks::Cache` (ленивая загрузка
  ключей по `AUTH_SERVICE_URL`, повторный поход при неизвестном `kid`),
  `jwt::verify_rs256` и `jwt_claims::check(payload, aud, scope)`. Ими
  пользуются wallet-ai и wallet-report.
- Обмен JWT → JWT для wallet-report даёт google-auth (`add-token-exchange`).
- Мотивация — в proposal.md, требования — в `specs/mcp-access/spec.md`.

## Goals / Non-Goals

**Goals:**
- Принимать JWT для wallet-mcp, не меняя поведение для `wmcp_…`.
- Не пробрасывать входящий JWT (MCP authorization, token passthrough).
- Один кеш и одна логика повтора после `401` от wallet-report для обоих видов.

**Non-Goals:**
- OAuth-метаданные (RFC 9728) и `resource_metadata` в `WWW-Authenticate`.
- Разные тексты `401` для `wmcp_…` и JWT. Модель получает тот же `message`.
  wallet-ai по коду `401` всё равно завершает задание.

## Decisions

### D1. Вид учётных данных — по префиксу
`wmcp_` + 43 символа → путь PAT (как сейчас). Всё остальное → путь JWT, если
он включён (`AUTH_EXCHANGE_SECRET` задан), иначе `401`. Пробовать «сначала
JWT, потом PAT» не нужно: форматы не пересекаются.

### D2. JWT проверяется локально, обмен — только после проверки
`jwks::Cache::find(kid)` → `jwt::verify_rs256` → `jwt_claims::check(payload,
"wallet-mcp", "mcp:read")`. Только после этого — обмен в google-auth. Мусор
и токены веб-сессии не доходят до google-auth. Это сохраняет сценарий
«JWT веб-сессии → `401`, в google-auth не отправляется».
Если `find` вернул `nullopt` (google-auth недоступен или неизвестный `kid`),
ответ `401`. Различить причины `jwks::Cache` не умеет, а wallet-ai на `401`
завершает задание с понятной ошибкой. Правка `jwks::Cache` ради `503` в
этом change не нужна.

### D3. Кеш: ключ — хэш предъявленного токена, срок — min двух exp
`TokenCache` получает второй источник обмена: интерфейс
`Exchanger { Exchange exchange(const std::string& presented) }` с двумя
реализациями (PAT → `/internal/mcp/token`, JWT → `/internal/token/exchange`).
`valid_until` = min(`exp` JWT для wallet-report, `exp` входящего JWT) − 30 с.
Входящий JWT проверяется на каждом запросе до обращения к кешу (D2), так что
истёкший входящий JWT получает `401` даже при живой записи в кеше.
Альтернатива — отдельный кеш для JWT. Отклонена: логика повтора в
`UserReports` (`refresh` → `exchange`) одна и та же.

### D4. Клиент обмена RFC 8693
`auth::AuthApi` получает метод
`exchange_jwt(subject_jwt) → Exchange` (form-тело, `X-Service-Secret`).
Разбор ответов:
- `200` с `access_token` → Ok;
- `400` (`invalid_grant`, `invalid_target`, `invalid_scope`, `invalid_request`) → Invalid;
- `401` `invalid_client` → Unavailable и `applog::error("token_exchange_invalid_client")`.
  Неверный секрет — проблема конфигурации, а не токена пользователя;
- сеть, `5xx`, `503` → Unavailable.

Разбор — чистая функция ради юнит-тестов, как `parse_exchange_response`
сейчас.

### D5. Журнал и метрики
В `mcp_http` после аутентификации — `applog::set_user(sub)` (уже есть) и
поля `credential` (`pat`/`jwt`) и `actor` (цепочка `act.sub` через `,`,
например `wallet-ai`). Метрика `mcp_tool_calls_total` получает метку
`credential`. Счётчик обменов `mcp_token_exchange_total` — метку `kind`
(`pat`/`jwt`).

### Правки в других сервисах
- **google-auth:** нужен `add-token-exchange` и секрет `wallet-mcp` в
  `TOKEN_EXCHANGE_SECRETS`.
- **wallet-infra:** `AUTH_EXCHANGE_SECRET` в `.env` wallet-mcp (test, prod).
- **wallet-gateway, wallet-report, wallet-common:** без правок.

## Risks / Trade-offs

- [Два обмена на первый вызов из чата: wallet-ai → google-auth, затем
  wallet-mcp → google-auth] → оба кешируются: wallet-ai держит свой токен на
  задание, wallet-mcp — по хэшу токена. Внутри одного вопроса (до 5 вызовов
  инструментов) google-auth зовётся по разу с каждой стороны.
- [JWKS недоступен → `401`, а не `503`] → для чата это «задание не удалось»,
  как и при `503`. Различие нужно внешним MCP-клиентам, его можно сделать
  вместе с OAuth.
- [Утёкший JWT для wallet-mcp] → живёт не больше 300 с, только чтение, тот же
  объём, что `reports:read` у самого пользователя.

## Migration Plan

1. Выкатить с пустым `AUTH_EXCHANGE_SECRET`: поведение как сейчас.
2. После google-auth (`add-token-exchange`) задать секрет в wallet-infra и
   перезапустить. Проверка — `curl` с JWT, полученным обменом.
3. Откат: очистить `AUTH_EXCHANGE_SECRET`.

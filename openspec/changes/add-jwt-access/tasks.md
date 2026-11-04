# Tasks

## 1. Клиент обмена JWT

- [x] 1.1 Добавить в `auth::AuthApi` метод `exchange_jwt` (form-тело RFC 8693, `audience=wallet-report`, `X-Service-Secret`) и чистую функцию разбора ответа по D4. Проверка: юнит-тесты разбора — `200`, `400 invalid_grant` → Invalid, `401 invalid_client` → Unavailable, битый `200` → Unavailable — `tests/test_auth_client.cpp`: `auth_parse_exchange_*`, `auth_client_exchange_jwt_*`

## 2. Проверка JWT и кеш

- [x] 2.1 Вид учётных данных по префиксу (D1) и локальная проверка JWT: `jwks::Cache` → `verify_rs256` → `jwt_claims::check(…, "wallet-mcp", "mcp:read")`. Проверка: юнит-тесты на ключе из теста — валидный JWT, чужой `aud`, нет `mcp:read`, истёкший, чужая подпись; JWT веб-сессии не вызывает обмен (фейковый `AuthApi` считает вызовы) — `tests/test_access.cpp`: `access_verify_jwt_*`; `tests/test_mcp_http.cpp`: `http_web_session_jwt_401_without_exchange`, `http_jwt_other_audience_or_scope_or_signature_401`
- [x] 2.2 Обобщить `TokenCache` на два обменника; `valid_until` = min двух `exp` − 30 с (D3). Проверка: юнит-тесты с фейковыми часами — серия вызовов даёт один обмен; входящий JWT истёк — `401` при живой записи в кеше; повтор после `401` от wallet-report работает для JWT — `tests/test_access.cpp`: `access_cache_jwt_series_one_exchange`, `access_cache_jwt_does_not_outlive_incoming_exp`, `access_report_401_refresh_works_for_jwt`
- [x] 2.3 Ответы `401`/`503` для пути JWT по спеке. Неверный секрет обмена → `503` и `token_exchange_invalid_client` в журнале. Проверка: юнит-тест `mcp_http` с фейковым `AuthApi` — `tests/test_mcp_http.cpp`: `http_jwt_exchange_rejected_401`, `http_jwt_invalid_client_503_and_pat_still_works`

## 3. Журнал, метрики, конфиг

- [x] 3.1 Поля журнала `credential` и `actor`, метки `credential` у `mcp_tool_calls_total` и `kind` у `mcp_token_exchange_total` (D5). Проверка: `GET /metrics` после запросов с `wmcp_…` и JWT показывает обе метки; в журнале нет токенов — `tests/test_mcp_http.cpp`: `http_metrics_have_credential_and_kind` (тело `/metrics` = `metrics::render() + metrics2::render()`), `http_jwt_for_wallet_mcp_is_served_with_exchanged_token` (журнал без токенов)
- [x] 3.2 Читать `AUTH_EXCHANGE_SECRET`; без него — предупреждение `JWT access disabled (AUTH_EXCHANGE_SECRET empty)`, путь JWT выключен. Добавить переменную в `.env.example` и таблицу конфигурации `README.md`, описать в README два вида токена. Проверка: старт без секрета — предупреждение в логе, `wmcp_…` работает — проверено контейнером 2026-11-04: без секрета в логе `JWT access disabled (AUTH_EXCHANGE_SECRET empty)`, JWT → `401` (`reason: format`); с секретом при недоступном google-auth порт открывается, JWKS грузится на первом JWT

## 4. Сквозная проверка на test-контуре

- [ ] 4.1 После выката google-auth `add-token-exchange`: получить токен бота, обменять от имени wallet-ai на `wallet-mcp`, вызвать `tools/call` `spending_by_category` напрямую на `wallet-mcp:8088/api/mcp/`. Проверка: ответ совпадает с вызовом тем же пользователем через `wmcp_…`; в журнале `credential=jwt`, `actor=wallet-ai`
- [ ] 4.2 Регрессия `wmcp_…`: LM Studio с прежним `mcp.json` отвечает на «сколько за няню в сентябре». Проверка: ответ как в README («Проверено на живой LM Studio»)

## Workflow follow-up

- Задать `AUTH_EXCHANGE_SECRET` в wallet-infra для test и prod.
- Архивировать после сквозной проверки и выката wallet-ai `add-chat-agent`.

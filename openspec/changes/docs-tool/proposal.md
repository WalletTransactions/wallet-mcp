# Proposal

## Why

MCP-клиент (LM Studio сейчас, чат wallet-ai и любой другой клиент позже) умеет отвечать
только про данные кошелька: траты, балансы, операции. Вопрос «как это работает?» —
«как считается баланс на дату», «что делает переоценка», «как выпустить MCP-токен» —
модель вынуждена выдумывать. Сервис `wallet-docs` уже отвечает на такие вопросы по
OpenSpec-спекам, но доступен только клиентам Android и web через `/api/docs/ask` с их
собственным токеном. MCP-токен и JWT обмена `wallet-docs` намеренно не получают
(change `docs-audience` в google-auth), поэтому из MCP справка недоступна.

## What Changes

- **wallet-mcp**: восьмой инструмент `ask_docs` (аргумент `question`) — вопрос о том,
  как устроена система и приложение; сервис передаёт его в `wallet-docs` и возвращает
  ответ с источниками. Инструмент необязателен: пока `WALLET_DOCS_URL` пуст, его нет в
  `tools/list`, остальные семь работают как прежде.
- **wallet-mcp**: для вызова `wallet-docs` сервис получает у google-auth второй короткий
  JWT (`aud` = `wallet-docs`, `scope` = `docs:ask`) и кеширует его по тому же правилу,
  что JWT для wallet-report.
- **google-auth** (отдельный change `docs-mcp-audience`): обмен MCP-токена принимает
  необязательный `audience` (`wallet-docs`), а таблица политики RFC 8693 получает пару
  «actor `wallet-mcp` → `wallet-docs`». JWT по `wmcp_…` несёт `platform` = `mcp`, JWT
  обмена наследует `platform` исходной сессии (`web`/`android`); `role` не несёт ни один.
- **wallet-docs** (отдельный change `mcp-scope`): таблица области получает платформу
  `mcp` — `wallet-web`, `wallet-web-eqdefault` и `wallet-mcp`, только `spec`.
- Данные кошелька инструмент не читает и не меняет: инвариант «только чтение» и запрет
  на `user_id`/`ledger_id` в аргументах сохраняются.

## Capabilities

### New Capabilities
- `docs-lookup`: инструмент `ask_docs` — аргумент, вид ответа, источники, тексты ошибок,
  условие появления в `tools/list` и получение JWT для wallet-docs.

### Modified Capabilities
- `wallet-tools`: состав инструментов — семь, а при заданном `WALLET_DOCS_URL` восемь.

## Impact

- **wallet-mcp**: `src/` (клиент wallet-docs, получение JWT для него, инструмент), `README.md`
  (таблица инструментов, переменная `WALLET_DOCS_URL`), `openapi.yaml` не меняется
  (описание инструментов живёт в `tools/list`), `.env.example`.
- **google-auth**: `/internal/mcp/token`, таблица политики обмена (спеки `mcp-tokens`,
  `token-exchange`) — отдельный change.
- **wallet-docs**: `scope_for` и его тест — отдельный change.
- **wallet-infra**: `WALLET_DOCS_URL` в `wallet-mcp.env` test/prod; индекс `wallet-mcp`
  в `docs-index.sh` — проверить, что репозиторий в списке.
- **wallet-gateway** не меняется: `wallet-mcp` ходит в `wallet-docs` напрямую по сети
  контура, как в wallet-report.
- **Чат wallet-ai**: инструмент появится в `tools/list` и у чата; влияние на выбор
  инструмента маленькой моделью — риск в design.
- Порядок выкатки: google-auth → wallet-docs → wallet-mcp → `WALLET_DOCS_URL` в окружении.

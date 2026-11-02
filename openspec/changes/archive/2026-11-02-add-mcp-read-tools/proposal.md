# Proposal

## Why

Хочется спрашивать про свои кошельки обычными словами — «сколько ушло на няню и
еду в сентябре», «что на карте», «чем сентябрь отличается от августа» — из
LM Studio, а позже из любого MCP-клиента (Claude Code, Claude Desktop). Данные
для ответов уже считает wallet-report. Нет стандартного способа дать модели
клиента безопасный доступ к ним. План `org/docs/TODO_wallet_ai.md` (пункт 10)
ставил `/mcp` в wallet-ai, но MCP-серверу не нужны ни LLM, ни очередь к GPU:
модель приносит клиент. Поэтому это отдельный тонкий сервис, который не
падает вместе с ИИ и не требует scope `ai:use`.

## What Changes

- **Новый сервис `wallet-mcp`** — C++-каркас по образцу wallet-report:
  `/healthz`, `/version`, `/metrics`, `/openapi.yaml`, `/docs`, JSON-логи.
- **Транспорт MCP Streamable HTTP**: `initialize`, `tools/list`, `tools/call`
  через JSON-RPC на одном эндпоинте под `/api/mcp/`. В `initialize` сервер
  отдаёт `instructions`: сегодняшнюю дату в поясе пользователя и правило «не
  считай сам, используй инструменты». Потоковых ответов (SSE) в первой версии
  нет.
- **Аутентификация каждого запроса** заголовком `Authorization: Bearer wmcp_…`.
  Сервис меняет токен на JWT через google-auth `POST /internal/mcp/token` и
  кеширует JWT до `exp` по хэшу токена. `Mcp-Session-Id` личность не
  подтверждает. Без токена, с отозванным или истёкшим токеном ответ `401`,
  и модель получает понятный текст: «MCP-токен недействителен или истёк —
  выпустите новый в веб-кабинете: Настройки → Устройства».
- **Шесть инструментов только для чтения:**
  - `list_wallets` — кошельки с балансами;
  - `list_categories` — категории без системных;
  - `spending_by_category(from, to, categories?, compare_from?, compare_to?)` —
    расход по категориям и итог по выбранным, со сравнением периодов
    (`/expense-structure`);
  - `income_expense_by_month(from, to)`;
  - `balance(wallet?, date?)`;
  - `search_operations(from, to, text?, categories?, wallets?, kind?, limit?)` —
    поиск с итогом `total` от SQL (`/operations` с фильтрами).
- **Имена вместо uuid в аргументах.** Имена кошельков и категорий
  разрешаются `resolver`'ом из wallet-common: точное совпадение, затем
  уникальный префикс, без учёта регистра, `ё` = `е`. При неоднозначности —
  ошибка инструмента со списком кандидатов, чтобы модель переспросила.
- **Ответы компактные и с эхом:** каждый ответ содержит период и валюту
  эквивалента, по которым он посчитан. Ошибка модели в датах видна в ответе.
  Валюта эквивалента — валюта первого кошелька справочника (кошелёк по
  умолчанию хранит только бот, design D8). Её можно задать аргументом.
- **Вне объёма:** запись и черновики трат (`draft_expense`), клиентский разбор
  выписок, OAuth 2.1 и `/.well-known/oauth-protected-resource`, SSE. Это
  отдельные changes позже.

## Capabilities

### New Capabilities
- `mcp-transport`: MCP Streamable HTTP — методы JSON-RPC, `initialize` с
  `instructions`, сессии, ошибки протокола.
- `mcp-access`: аутентификация запроса персональным токеном, обмен на JWT,
  кеш, отказы и их тексты, отсутствие параметров чужого доступа.
- `wallet-tools`: набор инструментов, их аргументы, разрешение имён, эхо
  периода и валюты, источник каждого итога в wallet-report.
- `service-endpoints`: liveness, версия, метрики (вызовы инструментов по
  имени и исходу, обмены токена), самоописание API, логи без сумм и текстов.

### Modified Capabilities
<!-- нет: репозиторий новый -->

## Impact

- **Новый репозиторий** `WalletTransactions/wallet-mcp`: пока создан только
  локально, в GitHub-организации его ещё нет. Нужны CI-сборка образа в
  `ghcr.io` и `third_party/wallet-common/` через `scripts/vendor.sh`.
- **Зависимости (changes в других репозиториях):**
  - google-auth `add-mcp-personal-tokens` — выпуск токена и
    `/internal/mcp/token`;
  - wallet-report `add-operations-filters` и wallet-api
    `add-operations-filter-functions` — без них нет `search_operations`.
    Остальные пять инструментов работают на текущем API;
  - wallet-gateway `add-mcp-route` — маршрут `/api/mcp/` без JWT-гейта;
  - wallet-infra `add-wallet-mcp-service` — compose, мониторинг, выкат;
  - wallet-web `add-mcp-tokens-ui` — выпуск токена пользователем.
- **wallet-common:** возможно, вынос `resolver` и клиента wallet-report в
  общую часть. Сейчас они там уже есть (`expense/`, `clients/`), нужно сверить
  в design.
- **Планы:** `org/docs/TODO_wallet_ai.md` пункт 10 и
  `org/docs/TODO_telegram_bot.md` §4a.3 нужно обновить: MCP переезжает в
  `wallet-mcp`, а будущий `chat` в wallet-ai может стать MCP-клиентом этого
  сервиса.

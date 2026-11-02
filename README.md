# wallet-mcp

MCP-сервер стека WalletTransactions: спрашивать про свои кошельки обычными
словами из LM Studio, а позже из любого MCP-клиента (Claude Code, Claude
Desktop). Модель приносит клиент, сервис только исполняет инструменты.
Change — [`openspec/changes/add-mcp-read-tools`](openspec/changes/add-mcp-read-tools/).

```
MCP-клиент ─ Bearer wmcp_… ─> wallet-gateway ─ /api/mcp/ ─> wallet-mcp ─ JWT ─> wallet-report
                                                               └─ POST /internal/mcp/token ─> google-auth
```

**Только чтение.** Данные берутся из wallet-report токеном пользователя.
У сервиса нет своей базы, прав на базу кошелька и инструментов записи.
Суммы считает SQL в wallet-report. Сервис только сводит готовые строки,
например в итог по выбранным категориям, и делает это точной десятичной
арифметикой, а не `double`.

## Доступ

- Каждый запрос к `/api/mcp/`, включая `initialize` и уведомления, несёт
  `Authorization: Bearer wmcp_…`. Персональный токен выпускается в
  веб-кабинете (google-auth `POST /auth/mcp-tokens`, change
  `add-mcp-personal-tokens`).
- Сервис меняет токен на JWT через google-auth `POST /internal/mcp/token`
  (aud `wallet-report`, scope `reports:read`, 300 с). Ручка служебная:
  гейтвей её не проксирует. JWT кешируется в памяти по SHA-256 токена, пока
  до `exp` больше 30 с. Сам токен сервис не хранит, подпись JWT не
  проверяет (это делает wallet-report).
- Отказы: нет токена, токен не `wmcp_…` или google-auth ответил `401` —
  `401 {"error":"invalid_token","message":"MCP-токен недействителен или истёк — выпустите новый в веб-кабинете: Настройки → Устройства"}`
  с `WWW-Authenticate: Bearer`. google-auth недоступен — `503`. Если
  wallet-report ответил `401` на JWT из кеша, сервис один раз меняет токен
  заново и повторяет вызов.
- Отзыв MCP-сессии в «Моих устройствах» действует не позже чем через 5 минут:
  столько живёт уже выданный JWT.
- `Mcp-Session-Id` выдаётся на `initialize` и личность не подтверждает.
  Сессий сервис не хранит, поэтому рестарт клиентов не разрывает.
- У инструментов нет параметров `user_id`/`ledger_id`/`as_user_id`: такой
  аргумент — ошибка `-32602`.

## Эндпоинты

| Метод | Путь | Что |
| --- | --- | --- |
| POST | `/api/mcp/` | JSON-RPC 2.0: `initialize`, `ping`, `tools/list`, `tools/call`; уведомления — `202` без тела |
| GET, DELETE | `/api/mcp/` | `405`: потока (SSE) и сессий нет |
| GET | `/healthz` | `{"status":"ok"}`, зависимости не проверяет, в access-лог не пишется |
| GET | `/version` | `{service, version, git_commit}` |
| GET | `/metrics` | Prometheus, в access-лог не пишется |
| GET | `/openapi.yaml`, `/docs` | спецификация и Swagger UI (вендоренный, без CDN) |

Порядок проверок на `POST /api/mcp/`: тело больше 64 КБ — `413` (до
аутентификации); токен (`401`/`503`); заголовок `MCP-Protocol-Version` с
незнакомой версией — `400`; разбор JSON-RPC. Ошибки протокола приходят с
HTTP `200`: `-32700` (не JSON), `-32600` (не JSON-RPC 2.0, батч), `-32601`
(неизвестный метод), `-32602` (неизвестный инструмент, аргументы не по
схеме). Путь без завершающего `/` не обслуживается.

Версии протокола MCP: `2025-11-25`, `2025-06-18`, `2025-03-26`. Сервис
отвечает той, что запросил клиент, иначе `2025-11-25`. Ревизия
`2026-07-28` (без `initialize`, `server/discover`) не поддерживается.
Клиент, умеющий обе, получает `400` на незнакомый `MCP-Protocol-Version` и
переходит на рукопожатие.

## Инструменты

Аргументы — имена, а не uuid. Имя сравнивается без учёта регистра, `ё` = `е`:
сначала точное совпадение, затем уникальный префикс. Неоднозначное или
ненайденное имя — `isError` со списком, чтобы модель переспросила. Даты —
`ГГГГ-ММ-ДД`, обе границы периода включаются. `currency` (ISO) необязателен:
по умолчанию берётся валюта первого кошелька справочника. Каждый ответ —
компактный JSON с `period` и `currency`, по которым он посчитан. Суммы —
строки, по `precision` валюты.

| Инструмент | Аргументы | Ответ | wallet-report |
| --- | --- | --- | --- |
| `list_wallets` | `currency?` | `wallets[{name, currency, balance}]`, `total`, `incomplete`, `date` | `accounts`, `balance?date=<завтра>` |
| `list_categories` | — | `categories[]` без системных и архивных | `accounts` |
| `spending_by_category` | `from`, `to`, `categories?`, `compare_from?`, `compare_to?`, `currency?` | `rows[{category, amount, amount_before?, delta?}]`, `total`, `total_before?`, `incomplete` | `expense-structure` ×1 или ×2 |
| `income_expense_by_month` | `from`, `to`, `currency?` | `months[{month, income, expense}]`, `incomplete` | `income-expense-by-month` |
| `balance` | `wallet?`, `date?`, `currency?` | как `list_wallets` на конец дня `date` | `balance?date=<date+1>` |
| `search_operations` | `from`, `to`, `text?`, `categories?`, `wallets?`, `kind?`, `limit?`, `currency?` | `operations[{date, kind, amount, currency, category, wallet \| from, to, comment}]`, `has_more`, `total`, `count`, `incomplete`, `kind` | `operations` с `category_uuid`/`wallet_uuid`/`kind`/`q`/`limit` |

Детали:

- **`spending_by_category`.** С `categories` в ответе только эти строки.
  Категория без трат даёт сумму `0.00` (по `precision` валюты). `total` — точная сумма строк, строка
  без курса в него не входит и ставит `incomplete: true`. Без `categories`
  `total` берётся из `total_eq` wallet-report. Сравнение периодов делается
  вторым вызовом `expense-structure`: ответ с `cmp_*` даёт доли, но не суммы.
- **`balance`, `list_wallets`.** Баланс на конец дня запрашивается как
  `/balance?date=<следующий день>`, потому что граница там не включается.
  «Сегодня» считается в поясе `MCP_TZ`. Итог всех кошельков — `total_eq`
  wallet-report. В нём есть и архивные кошельки, как в вебе.
- **`search_operations`.** `kind` по умолчанию `expense`, `total` при этом
  неотрицательный. `kind: "any"` снимает фильтр типа, и тогда `total` —
  сальдо: доходы минус расходы, прямые переводы не входят. `total` и
  `count` — по всем подходящим операциям периода (SQL), а не только по
  показанным. При `any` без других фильтров итога нет. `limit` — 1..50, по
  умолчанию 20. Комментарий обрезается до 120 байт. Если фильтр ушёл, а
  итога в ответе нет (wallet-report без фильтров ленты), инструмент
  отвечает «Отчёты временно недоступны».
- **Ошибки.** Аргумент не по `inputSchema` (лишний ключ, неверный тип, вне
  enum или диапазона) — JSON-RPC `-32602`. `null` у необязательного
  аргумента считается отсутствием. Ошибка смысла — `isError: true` с
  текстом: дата не существует, `from` позже `to`, задана одна граница
  сравнения, имя не найдено или неоднозначно, неизвестная валюта, «Кошельков
  пока нет — заведите их в приложении.». wallet-report не ответил или
  ответил не `200`/`401` — «Отчёты временно недоступны, попробуйте позже.».

Справочник счетов и валют кешируется на 60 с по хэшу токена. Если имя не
найдено, справочник перечитывается один раз: категорию могли только что
завести в приложении.

## Переменные окружения

| Переменная | По умолчанию | Что |
| --- | --- | --- |
| `AUTH_SERVICE_URL` | — (обязательна) | google-auth для `POST /internal/mcp/token`, по сети контура |
| `WALLET_REPORT_URL` | — (обязательна) | wallet-report напрямую, не через гейтвей |
| `PORT` | `8088` | |
| `HOST` | `127.0.0.1` | в контейнере — `0.0.0.0` |
| `MCP_TZ` | `UTC` | пояс IANA для «сегодня»; неизвестный — сервис не стартует |
| `LOG_DIR` | stdout | папка — файл на день |
| `LOG_LEVEL` | `debug` | `error`/`warn`/`info`/`debug` |

Без обязательной переменной сервис пишет `<KEY> must be set — …` и
завершается с кодом `1`. `.env` в рабочей директории читается при старте,
переменные окружения процесса приоритетнее. Пример — `.env.example`.

Таймауты: google-auth — соединение 3 с, ответ 5 с. wallet-report — 5 с и
15 с (`clients/reports_client` из wallet-common). Гейтвей ждёт 30 с.

## Логи и метрики

JSON-логи (`util/log` wallet-common) с `trace_id` из `X-Trace-Id`, сервис
свой не порождает. На вызов инструмента — строка `mcp_tool_call` с
`user_id`, `tool`, `result` и `duration_ms`. Аргументы, суммы, имена
счетов, комментарии, тела запросов и ответов, токены и их хэши в лог не
пишутся. Отказ по токену — `mcp_auth_rejected` с причиной
(`missing`/`format`/`google_auth`/`wallet_report`).

Метрики:

- `build_info`, `http_requests_total`, `http_request_duration_seconds`;
- `mcp_tool_calls_total{tool,result}` — `ok`/`tool_error`/`unavailable`;
- `mcp_tool_duration_seconds{tool}`;
- `mcp_token_exchange_total{result}` — `ok`/`invalid`/`unavailable`;
- `mcp_token_cache_total{result}` — `hit`/`miss`;
- `mcp_reports_errors_total{kind}` — `network`, `http_<код>`.

Счётчик с двумя лейблами — `src/metrics2.*`: `util/metrics` wallet-common
умеет только один лейбл.

## Сборка и тесты

```bash
docker build --target test -f Containerfile .                # юнит-тесты (ctest)
docker build --build-arg GIT_COMMIT=$(git rev-parse --short=8 HEAD) -t wallet-mcp .
```

`BUILD_JOBS` по умолчанию 3: C++-сервисы собираются по одному. Общий код
(`util/`, `expense/`, `clients/`) лежит в `third_party/wallet-common/`.
Это копия wallet-common: правится там и раскладывается
`wallet-common/scripts/vendor.sh`, сверка — `vendor.sh --check`.

Сквозная проверка на стенде — `scripts/smoke.sh`:

```bash
MCP_TOKEN=wmcp_… bash scripts/smoke.sh http://<хост>:8090/api/mcp/ 2026-09-01 2026-09-30
```

## Подключение LM Studio

`mcp.json` (Program → Install → Edit mcp.json):

```json
{
  "mcpServers": {
    "wallet": {
      "url": "http://<хост>:8090/api/mcp/",
      "headers": {"Authorization": "Bearer wmcp_…"}
    }
  }
}
```

Порт — контур, на котором выпущен токен (prod `:8090`, test `:8091`); `/` в
конце `url` обязателен. Готовый конфиг даёт веб: «Настройки» → «Устройства» →
«Подключить MCP-клиент» → «Скопировать конфиг». Несколько серверов — разные
ключи внутри `mcpServers`; одновременно включённые test и prod дают модели два
одинаковых набора инструментов — для работы держите включённым один.

**Проверено на живой LM Studio 2026-11-02** (задача 7.3, test):

- LM Studio 0.4.26, `initialize`: `protocolVersion` `2025-11-25`, `clientInfo`
  `lmstudio-mcp-server-session 1.0.0`; `tools/list` — шесть инструментов.
- Модель `google/gemma-4-e4b`. Вопрос «сколько я потратил на няню и еду в
  сентябре?» → `spending_by_category` с `from` 2026-09-01, `to` 2026-09-30 и
  `categories` «Няня», «Продукты» → ошибка инструмента со списком похожих
  (категории в кошельке — по-английски) → модель переспросила и вызвала с
  «Baby Sitter», «Groceries» → ответ «3503.22 EUR» (2899.00 + 604.22) совпал с
  `/api/reports/expense-structure` за тот же период.
- LM Studio шлёт `tools/list` повторно перед каждым ходом модели и
  `notifications/cancelled` между ходами — это нормально.

## Минимум внешних зависимостей

Как у остальных сервисов: `httplib.h`, `nlohmann/json.hpp` и Swagger UI
вендорены в `third_party/`, юнит-тесты — на самодельном харнессе
`tests/test_framework.hpp`. Из системы нужен только OpenSSL.

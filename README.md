# wallet-mcp

MCP-сервер стека WalletTransactions: спрашивать про свои кошельки обычными
словами из LM Studio, а позже из любого MCP-клиента (Claude Code, Claude
Desktop). Модель приносит клиент, сервис только исполняет инструменты.
Change — [`openspec/changes/add-mcp-read-tools`](openspec/changes/add-mcp-read-tools/).

```
MCP-клиент ─ Bearer wmcp_… ─> wallet-gateway ─ /api/mcp/ ─> wallet-mcp ─ JWT ─> wallet-report
wallet-ai  ─ Bearer JWT (aud wallet-mcp) ────────────────────>    │
                                                                  ├─ POST /internal/mcp/token ─────> google-auth
                                                                  └─ POST /internal/token/exchange ─> google-auth
```

**Только чтение.** Данные берутся из wallet-report токеном пользователя.
У сервиса нет своей базы, прав на базу кошелька и инструментов записи.
Суммы считает SQL в wallet-report. Сервис только сводит готовые строки,
например в итог по выбранным категориям, и делает это точной десятичной
арифметикой, а не `double`.

## Доступ

- Каждый запрос к `/api/mcp/`, включая `initialize` и уведомления, несёт
  `Authorization: Bearer <токен>` одного из двух видов (вид — по префиксу):
  - **`wmcp_…`** — персональный токен из веб-кабинета (google-auth
    `POST /auth/mcp-tokens`, change `add-mcp-personal-tokens`). Так
    подключаются LM Studio и другие MCP-клиенты;
  - **JWT для wallet-mcp** — RS256 от google-auth с `aud` ∋ `wallet-mcp` и
    `scope` ∋ `mcp:read`. Его получает обменом wallet-ai (чат), change
    `add-jwt-access`. Сервис проверяет подпись по JWKS google-auth
    (`AUTH_SERVICE_URL/.well-known/jwks.json`, ключи грузятся при первом JWT),
    `iss`, `aud`, `scope` и `exp` локально и только потом идёт в google-auth.
    JWT веб-сессии (`aud` без `wallet-mcp`) получает `401` без обмена.
    Приём JWT включается переменной `AUTH_EXCHANGE_SECRET`.
- Предъявленный токен в wallet-report не уходит. Сервис получает свой JWT
  для wallet-report (aud `wallet-report`, scope `reports:read`, до 300 с):
  для `wmcp_…` — `POST /internal/mcp/token`, для JWT — обменом RFC 8693
  `POST /internal/token/exchange` (actor `wallet-mcp`,
  `audience=wallet-report`, `X-Service-Secret` из `AUTH_EXCHANGE_SECRET`).
  Ручки служебные: гейтвей их не проксирует. JWT кешируется в памяти по
  SHA-256 предъявленного токена, пока до `exp` больше 30 с; для входящего JWT
  запись не живёт дольше его собственного `exp`. Сам токен сервис не хранит,
  подпись JWT для wallet-report не проверяет (это делает wallet-report).
- Отказы: нет токена, токен не `wmcp_…` и не годный JWT для wallet-mcp,
  google-auth ответил `401` на `/internal/mcp/token` или `400` на обмен —
  `401 {"error":"invalid_token","message":"MCP-токен недействителен или истёк — выпустите новый в веб-кабинете: Настройки → Устройства"}`
  с `WWW-Authenticate: Bearer`. google-auth недоступен — `503`. Неверный
  `AUTH_EXCHANGE_SECRET` (google-auth: `401 invalid_client`) — тоже `503` и
  ошибка `token_exchange_invalid_client` в журнале: это конфигурация, а не
  токен. Если wallet-report ответил `401` на JWT из кеша, сервис один раз
  меняет токен заново и повторяет вызов.
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
| GET | `/version` | `{service, version, build, git_commit, started_at}`: `build` — тег образа (`APP_BUILD`, без него `<VERSION>-dev`), `started_at` — время запуска, UTC |
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
| `spending_by_category` | `from`, `to`, `categories?`, `exclude_categories?`, `compare_from?`, `compare_to?`, `currency?` | `rows[{category, amount, amount_before?, delta?, count, avg_per_month, count_before?, avg_per_month_before?}]`, `total`, `total_before?`, `incomplete`, `count`, `months`, `avg_per_month`, `count_before?`, `months_before?`, `avg_per_month_before?` | `expense-structure` ×1 или ×2 |
| `spending_by_category_by_month` | `from`, `to`, `categories?`, `exclude_categories?`, `months_of_year?`, `currency?` | `rows[{category, amount, count, avg_per_month, by_month?}]`, `months_of_year?`, `total`, `count`, `months`, `avg_per_month`, `incomplete` | `expense-by-category-by-month` ×1 |
| `income_expense_by_month` | `from`, `to`, `currency?` | `months[{month, income, expense}]`, `incomplete` | `income-expense-by-month` |
| `balance` | `wallet?`, `date?`, `currency?` | как `list_wallets` на конец дня `date` | `balance?date=<date+1>` |
| `search_operations` | `from?`, `to?`, `text?` (массив вариантов), `categories?`, `exclude_categories?`, `wallets?`, `kind?`, `limit?`, `cursor?`, `currency?` | `operations[{date, kind, amount, currency, category, wallet \| from, to, comment}]`, `has_more`, `next_cursor?`, `total`, `count`, `incomplete`, `kind` | `operations` с `category_uuid`/`exclude_category_uuid`/`wallet_uuid`/`kind`/`q` (повторённый)/`limit`/`cursor_date`+`cursor_uuid` |
| `ask_docs` | `question` (до 1000 символов) | `answer`, `sources[{repo, capability, heading}]` — как есть от wallet-docs; данных кошелька не читает. Только при заданном `WALLET_DOCS_URL` | `POST {WALLET_DOCS_URL}/api/docs/ask` (wallet-docs) |

Детали:

- **`ask_docs`.** Необязательный восьмой инструмент: ответ на вопрос «как
  работает система, приложение или MCP» по OpenSpec-спекам сервиса
  wallet-docs. Без `WALLET_DOCS_URL` его нет в `tools/list`, `tools/call` с
  этим именем — `-32602`, остальные семь работают как прежде. Для вызова
  сервис берёт у google-auth отдельный JWT (`aud` = `wallet-docs`, `scope` =
  `docs:ask`, кеш отдельный от JWT для wallet-report); область разделов
  определяет wallet-docs по JWT, у инструмента нет аргументов области.
  Отказы — `isError` с текстами: «Слишком много вопросов, повторите через N
  с.» (6 в минуту на пользователя, общий лимит с вебом), «Справка временно
  недоступна, попробуйте позже.», «Справка недоступна для этого токена.»,
  «Вопрос не принят: …», «Вопрос пустой.». Текст вопроса и ответа в журнал и
  метрики не пишется: в `mcp_tool_call` только `sources` (число источников).
  Порядок включения: google-auth `docs-mcp-audience` → wallet-docs
  `mcp-scope` → wallet-mcp → `WALLET_DOCS_URL` в окружении последним.
- **`spending_by_category`.** С `categories` в ответе только эти строки.
  Категория без трат даёт сумму `0.00` (по `precision` валюты). `total` — точная сумма строк, строка
  без курса в него не входит и ставит `incomplete: true`. Без `categories`
  `total` берётся из `total_eq` wallet-report. Сравнение периодов делается
  вторым вызовом `expense-structure`: ответ с `cmp_*` даёт доли, но не суммы.
  `exclude_categories` — все категории, кроме перечисленных, `total` — точная
  сумма оставшихся строк. `count` — `op_count` wallet-report (у wallet-report
  без него — `null`). `months` — делитель среднего: сумма долей месяцев
  периода по дням («2024-11-05..2026-11-04» — `24.00`), `avg_per_month` —
  сумма / `months`, точной десятичной арифметикой. С периодом сравнения — то
  же с суффиксом `_before`: периоды разной длины сравниваются по среднему.
- **`spending_by_category_by_month`.** Матрица «категория × месяц» из
  `expense-by-category-by-month`; период — не длиннее 36 задетых месяцев.
  `months_of_year` (1..12 без повторов) оставляет только эти месяцы года:
  суммы, `count`, `by_month` и `months` считаются по ним, так что «лето» и
  «остальные месяцы» с одним периодом в сумме дают итог без фильтра.
  `by_month` («ГГГГ-ММ» → сумма, только непустые месяцы) — только при
  `categories`: без фильтра ответ на 90 категорий × 36 месяцев не влез бы в
  контекст маленькой модели. Без `categories`, `exclude_categories` и
  `months_of_year` `total` — `total_eq` wallet-report, иначе точная сумма
  строк.
- **`categories` и `exclude_categories`** вместе — `isError` «Задайте
  categories или exclude_categories, не оба.».
- **`balance`, `list_wallets`.** Баланс на конец дня запрашивается как
  `/balance?date=<следующий день>`, потому что граница там не включается.
  «Сегодня» считается в поясе `MCP_TZ`. Итог всех кошельков — `total_eq`
  wallet-report. В нём есть и архивные кошельки, как в вебе.
- **`search_operations`.** `kind` по умолчанию `expense`, `total` при этом
  неотрицательный. `exclude_categories` уходит в `exclude_category_uuid` и
  считается фильтром — итог есть и при `any`. `kind: "any"` снимает фильтр типа, и тогда `total` —
  сальдо: доходы минус расходы, прямые переводы не входят. `total` и
  `count` — по всем подходящим операциям периода (SQL), а не только по
  показанным. При `any` без других фильтров итога нет. `limit` — 1..50, по
  умолчанию 20. Комментарий обрезается до 120 байт. Если фильтр ушёл, а
  итога в ответе нет (wallet-report без фильтров ленты), инструмент
  отвечает «Отчёты временно недоступны».
  `text` — массив до 10 вариантов (каждый до 100 символов): подходит
  комментарий с любым из них («стриж», «парикмах», «barber» одним вызовом).
  Варианты уходят повторённым `q`; пустые после trim и повторы
  отбрасываются; операция под двумя вариантами в `total`/`count` входит
  один раз — это считает SQL (wallet-api 000032), а не сервис.
  При `has_more: true` в ответе есть `next_cursor` — строка
  `«<дата>|<uuid>»` из `next_cursor_date`/`next_cursor_uuid` wallet-report;
  её передают в `cursor` с теми же остальными аргументами. `total` и `count`
  на любой странице одинаковые. Курсор не той формы — `isError` «cursor
  неверен: передайте next_cursor из прошлого ответа без изменений.» до
  обращения к wallet-report; `has_more` без курсора в ответе wallet-report —
  «Отчёты временно недоступны».
  Период необязателен: без `to` — по сегодня в поясе `MCP_TZ`, без `from` — с
  начала истории (в wallet-report уходит `1900-01-01`, в ответе
  `period.from: null`). Так вопросы «когда последний раз…» решаются одним
  вызовом без даты от модели: LM Studio не передаёт модели `instructions`
  со строкой «Сегодня …», и маленькая модель подставляла выдуманную дату.
  `from` позже сегодня без `to` — «Период неверен: from (…) позже to (…).».
- **Ошибки.** Аргумент не по `inputSchema` (лишний ключ, неверный тип, вне
  enum или диапазона, повтор в `months_of_year`) — JSON-RPC `-32602`. `null` у необязательного
  аргумента считается отсутствием. Ошибка смысла — `isError: true` с
  текстом: дата не существует, `from` позже `to`, задана одна граница
  сравнения, имя не найдено или неоднозначно, неизвестная валюта, заданы
  оба списка категорий, помесячный период длиннее 36 месяцев, «В периоде нет
  выбранных месяцев.», «Кошельков
  пока нет — заведите их в приложении.». wallet-report не ответил или
  ответил не `200`/`401` — «Отчёты временно недоступны, попробуйте позже.».

Справочник счетов и валют кешируется на 60 с по хэшу токена. Если имя не
найдено, справочник перечитывается один раз: категорию могли только что
завести в приложении.

## Переменные окружения

| Переменная | По умолчанию | Что |
| --- | --- | --- |
| `AUTH_SERVICE_URL` | — (обязательна) | google-auth по сети контура: `POST /internal/mcp/token`, `POST /internal/token/exchange`, JWKS |
| `AUTH_EXCHANGE_SECRET` | пусто | `X-Service-Secret` wallet-mcp для обмена JWT (секрет `wallet-mcp` в `TOKEN_EXCHANGE_SECRETS` google-auth). Пусто — приём JWT выключен, в логе старта `JWT access disabled (AUTH_EXCHANGE_SECRET empty)`, `wmcp_…` работает |
| `WALLET_REPORT_URL` | — (обязательна) | wallet-report напрямую, не через гейтвей |
| `WALLET_DOCS_URL` | пусто | wallet-docs напрямую (`http://wallet-docs:8089`). Пусто — инструмента `ask_docs` нет, в логе старта `docs tool disabled (WALLET_DOCS_URL empty)` |
| `PORT` | `8088` | |
| `HOST` | `127.0.0.1` | в контейнере — `0.0.0.0` |
| `MCP_TZ` | `UTC` | пояс IANA для «сегодня»; неизвестный — сервис не стартует |
| `LOG_DIR` | stdout | папка — файл на день |
| `LOG_LEVEL` | `debug` | `error`/`warn`/`info`/`debug` |

Без обязательной переменной сервис пишет `<KEY> must be set — …` и
завершается с кодом `1`. `.env` в рабочей директории читается при старте,
переменные окружения процесса приоритетнее. Пример — `.env.example`.

Таймауты: google-auth — соединение 3 с, ответ 5 с. wallet-report — 5 с и
15 с (`clients/reports_client` из wallet-common). wallet-docs — 3 с и 25 с. Гейтвей ждёт 30 с.

## Логи и метрики

JSON-логи (`util/log` wallet-common) с `trace_id` из `X-Trace-Id`, сервис
свой не порождает. На вызов инструмента — строка `mcp_tool_call` с
`user_id`, `tool`, `result`, `credential` и `duration_ms`. В
`request_finished` запроса к `/api/mcp/` — `credential` (`pat` для `wmcp_…`,
`jwt` для JWT) и для JWT `actor` — цепочка `act` через запятую (из чата —
`wallet-ai`). Аргументы, суммы, имена счетов, комментарии, тела запросов и
ответов, токены и их хэши в лог не пишутся. Отказ по токену —
`mcp_auth_rejected` с причиной (`missing`/`format`/`google_auth`/
`wallet_report`, для JWT — `jwt_malformed`/`jwt_unknown_key`/`jwt_signature`/
`jwt_expired`/`jwt_wrong_audience`/`jwt_insufficient_scope`/… ).

Метрики:

- `build_info`, `http_requests_total`, `http_request_duration_seconds`;
- `mcp_tool_calls_total{tool,result,credential}` — `ok`/`tool_error`/`unavailable`, `pat`/`jwt`;
- `mcp_tool_duration_seconds{tool}`;
- `mcp_token_exchange_total{result,kind}` — `ok`/`invalid`/`unavailable`, `pat`/`jwt`;
- `mcp_token_cache_total{result}` — `hit`/`miss`;
- `mcp_reports_errors_total{kind}` — `network`, `http_<код>`.

Счётчики с несколькими лейблами — `src/metrics2.*`: `util/metrics`
wallet-common умеет только один лейбл.

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

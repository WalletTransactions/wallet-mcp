# Tasks

## 1. Репозиторий и каркас

- [x] 1.1 Завести репозиторий: `git init` в `wallet-mcp/`, `.gitignore` и `.dockerignore` по образцу wallet-report, `VERSION` (`0.1.0`), `README.md` (назначение, переменные окружения, пустая таблица инструментов). Создать `WalletTransactions/wallet-mcp` в GitHub-организации и запушить; проверка — `git remote -v` и первый коммит виден в GitHub
  Репозиторий в организации уже был (private, `origin` → `WalletTransactions/wallet-mcp`, коммиты OpenSpec и add-to-project на main). Ветка `add-mcp-read-tools`: `.gitignore`, `.dockerignore` (по образцу wallet-ai, как у wallet-report плюс `build-tests`), `VERSION` 0.1.0, `README.md` — сразу с заполненной таблицей инструментов.
- [x] 1.2 Вендорить зависимости: `third_party/httplib.h`, `third_party/nlohmann/`, `third_party/swagger-ui/` (как в wallet-report) и `third_party/wallet-common/` через `wallet-common/scripts/vendor.sh`; проверка — `vendor.sh --check` проходит
  httplib 0.53.1, nlohmann и swagger-ui 5.32.15 — копии из wallet-report; wallet-common 0.1.0 (607a5b0f) через `vendor.sh`; `vendor.sh --check` → `ok wallet-mcp`.
- [x] 1.3 `CMakeLists.txt` и `src/version.hpp.in` по образцу wallet-report, без libpq; цели `wallet_mcp` и `wallet_mcp_tests` (ctest); проверка — `docker build` собирает пустой `main`, `ctest` запускается (0 тестов)
  По образцу wallet-ai (там уже библиотека + юнит-тесты): `wallet_mcp_core`, `wallet_mcp`, `wallet_mcp_tests`, OpenSSL без libpq и zlib. Сразу с полным `main` и тестами: `docker build --target test` → ctest 1/1, 76 тестов.
- [x] 1.4 `Containerfile` (многостадийный, `BUILD_JOBS=3`, runtime с `tzdata` и `curl` для healthcheck, пользователь `appuser`); проверка — образ собирается, контейнер стартует и отвечает на `/healthz`
  Стадии deps/build/test/runtime как у wallet-ai. Образ собран (35.6 МБ), на изолированном стенде контейнер стартует и отвечает `/healthz` → `{"status":"ok"}`, `/version` с `git_commit` из `--build-arg`.
- [x] 1.5 GitHub Actions `ci.yml`, `release.yml`, `packages-cleanup.yml` по образцу wallet-report (образ `ghcr.io/wallettransactions/wallet-mcp`); проверка — CI зелёный на первом push, тег `v0.1.0-rc.1` публикует образ
  Итог 2026-11-02: CI на PR #1 и на main — зелёный; тег `v0.1.0-rc.1` → Release success, образ `ghcr.io/wallettransactions/wallet-mcp:0.1.0-rc.1` поднят в test (`/version` 0.1.0, 4038c3e6).
  Файлы сделаны: `ci.yml` (юнит-тесты в debian:bookworm-slim с tzdata + сборка образа), `release.yml` и `packages-cleanup.yml` — без изменений, как у wallet-report/wallet-ai (образ берётся из `${GITHUB_REPOSITORY,,}`). CI — на PR ветки. Открыто: тег `v0.1.0-rc.1` и публикация образа — после мержа.

## 2. Служебные эндпоинты (`service-endpoints`)

- [x] 2.1 `main.cpp`: `.env`, обязательные `AUTH_SERVICE_URL` и `WALLET_REPORT_URL` с текстом `<KEY> must be set — …`, `PORT` (8088), `MCP_TZ` (UTC, неизвестный пояс — выход с кодом 1), JSON-логи с `trace_id` из `X-Trace-Id`; проверка — запуск без переменной и с `MCP_TZ=Mars/Olympus` завершается кодом 1, с ними — слушает порт
  Проверено контейнером: без `WALLET_REPORT_URL` и без `AUTH_SERVICE_URL` — `… must be set — …`, exit 1; `MCP_TZ=Mars/Olympus` — ошибка, exit 1; `MCP_TZ=Europe/Moscow` и по умолчанию UTC — `listening on …:8088`. Пояс выставляется в TZ процесса до потоков.
- [x] 2.2 `docs.cpp`: `/healthz`, `/version`, `/openapi.yaml`, `/docs` (вендоренный Swagger UI), `/metrics` с `build_info` и гистограммой HTTP; `/healthz` и `/metrics` не пишутся в access-лог; проверка — curl каждого пути
  curl на стенде: все пути 200 с нужным Content-Type, `/docs` ссылается только на `/vendor/swagger-ui/*` (отдаются сервисом), в логе контейнера 36 строк `request_finished` и ни одной про `/healthz`/`/metrics`. `/metrics` = `util/metrics` + `metrics2` (счётчики с двумя лейблами).
- [x] 2.3 `openapi.yaml` сервиса: `POST /api/mcp/` (JSON-RPC, ответы `200`/`202`/`401`/`413`/`503`), `GET /api/mcp/` → `405`, служебные пути; README — раздел «Эндпоинты»; проверка — `/docs` показывает спецификацию
  Плюс `400` (незнакомый `MCP-Protocol-Version`, см. 4.3) и `DELETE` → `405`. YAML разбирается `yaml.safe_load`, `/openapi.yaml` и `/docs` отдаются на стенде. Открыто: визуально Swagger UI в браузере не открывал.

## 3. Доступ (`mcp-access`)

- [x] 3.1 Клиент google-auth `POST /internal/mcp/token` (таймауты 3 с/5 с, исходы: `ok`/`invalid`/`unavailable`); юнит-тест на фейковом HTTP-сервере (200, 401, 500, таймаут); проверка — `ctest`
  `src/auth_client.*`; тесты на локальном httplib-сервере: 200 (тело ровно `{"token"}`), 401, 500, битый 200, таймаут (укороченный), закрытый порт.
- [x] 3.2 Кеш JWT по SHA-256 токена до `expires_in − 30 с`, без хранения самого токена, с метриками `mcp_token_exchange_total` и `mcp_token_cache_total`; юнит-тесты: попадание, промах, истечение, сброс по `401` wallet-report и один повтор; проверка — `ctest`
  `src/access.*` (`TokenCache`, `UserReports`). Тесты: 5 вызовов за минуту — 1 обмен, до exp 31 с — кеш, 20 с — новый обмен, отказы не кешируются, 401 → новый JWT и повтор, второй 401 → отказ, повтор один на запрос, метрики hit/miss/ok.
- [x] 3.3 Проверка каждого запроса до разбора JSON-RPC: нет заголовка или нет префикса `wmcp_` → `401` с `{"error":"invalid_token","message":…}` и `WWW-Authenticate: Bearer`; google-auth недоступен → `503`; событие `mcp_auth_rejected` без токена и хэша; проверка — юнит-тесты на все ветки и curl без токена на живом стеке
  Юнит-тесты `test_mcp_http`: без заголовка, JWT вместо токена (без обмена), отозванный, google-auth down → 503 и подъём, initialize/уведомления тоже с токеном, двойной 401 wallet-report; в логе нет ни токена, ни хэша. curl — на изолированном стенде (заглушки google-auth и wallet-report): без токена и с JWT → 401 с текстом, стоп google-auth → 503, `/healthz` при этом 200. Через гейтвей — в 7.1.

## 4. Транспорт MCP (`mcp-transport`)

- [x] 4.1 Лимит тела 64 КБ → `413` до аутентификации; `GET /api/mcp/` → `405`; проверка — curl
  curl на стенде: 1 МБ → 413, обменов в google-auth не прибавилось; `GET` с `Accept: text/event-stream` → 405; `POST /api/mcp` без `/` → 404. То же — юнит-тест на httplib-сервере (64 КБ−1 → уже 401).
- [x] 4.2 Диспетчер JSON-RPC: `-32700`, `-32600` (включая батч), `-32601`, `ping`, уведомления → `202` без тела; юнит-тесты на каждый код; проверка — `ctest`
  `src/mcp.*`. Ответ клиента (без `method`, с `result`) — тоже 202.
- [x] 4.3 `initialize`: выбор версии протокола (D1), `capabilities`, `serverInfo`, `instructions` с датой в `MCP_TZ` и правилами, новый `Mcp-Session-Id` (UUID v4) без хранения; сверить с актуальной спецификацией MCP версии и заголовок `MCP-Protocol-Version`; юнит-тест на текст `instructions` с фиксированной датой; проверка — `ctest`
  Сверено с modelcontextprotocol.io 2026-11-02 (запись в design D1): версии `2025-11-25` (по умолчанию), `2025-06-18`, `2025-03-26`; `2026-07-28` (без рукопожатия) не поддерживается; незнакомый `MCP-Protocol-Version` → `400` (вместо «не проверяется» в D1), чтобы клиент двух ревизий откатился на `initialize`. Тест instructions: 2026-10-31, «суббота», «Не считай суммы сам — используй результаты инструментов.».
- [x] 4.4 `tools/list` и `tools/call` с реестром инструментов: проверка аргументов по схеме (`-32602` на лишние ключи, неверные типы, неизвестный инструмент), `content[0].text` + `isError`; юнит-тест, что `as_user_id` и `user_id` отклоняются; проверка — `ctest`
  Аргументы проверяются по той же `inputSchema`, что отдаёт `tools/list` (подмножество: type, enum, min/max, maxLength в символах, min/maxItems, required) — схема и проверка не расходятся. `null` у необязательного аргумента = отсутствие. Отклоняются `as_user_id`, `user_id`, `ledger_id`, `token`.

## 5. Общие части инструментов (`wallet-tools`)

- [x] 5.1 `src/decimal.*`: разбор, сложение, вычитание и вывод десятичных строк на `__int128` с масштабом 10^8 (D10); юнит-тесты: `0.10 + 0.20 + 1999.99 = 2000.29`, отрицательные, 12 знаков целой части, мусор во входе; проверка — `ctest`
  Плюс `format(precision)` с округлением половины от нуля — суммы в ответе по precision валюты (D11).
- [x] 5.2 Даты: разбор `ГГГГ-ММ-ДД` с проверкой существования, `from ≤ to`, перевод в границы wallet-report и «конец дня» для `/balance` (D6), «сегодня» в `MCP_TZ`; юнит-тесты (29 февраля, 31 апреля, смена года); проверка — `ctest`
  `src/dates.*`; плюс день недели для instructions и проверка имени пояса по tzdata (как `tz` бота). «Сегодня» в UTC и Europe/Moscow на одном моменте — разные даты.
- [x] 5.3 Справочник счетов через `reports_client` и `parse_accounts`, кеш 60 с по хэшу токена, повтор поиска при ненайденном имени, валюта по умолчанию — первый кошелёк, `currency` проверяется по справочнику валют (D8, D9); тексты «подходит к нескольким» и «не найдено» через `resolve_name`; юнит-тесты на фейковом `ReportsApi`; проверка — `ctest`
  `src/directory.*` + `Call` в `src/tools.cpp`. Справочник валют (`/currencies`) — тот же кеш 60 с, запрашивается только при явном `currency`; в тексте ошибки — валюты с `show_in_equivalent`.
- [x] 5.4 Ответ wallet-report не `200`/`401` или сеть → `isError` «Отчёты временно недоступны, попробуйте позже.», метрики `mcp_tool_calls_total`/`mcp_tool_duration_seconds`, лог вызова без аргументов и сумм; юнит-тест, что в лог не попадает текст `text`; проверка — `ctest`
  Поле ответа не того типа — тоже «недоступны», а не 500. Тест: в логе есть `tool`, `result`, `user_id`, нет «Маша», сумм, имён счетов, дат периода, токена и JWT; при отказе wallet-report в лог — только путь без query.

## 6. Инструменты (`wallet-tools`)

- [x] 6.1 `list_wallets` и `list_categories`; юнит-тесты на фейковом `ReportsApi` (системные категории скрыты, итог и `incomplete`); строки в таблице инструментов README; проверка — `ctest`
  Итог `list_wallets` — `total_eq` wallet-report (с архивными кошельками, как в вебе), список — неархивные кошельки; кошелёк без движения — `0.00`. `list_wallets` тоже принимает `currency` (правило «Валюта эквивалента»).
- [x] 6.2 `spending_by_category` с фильтром категорий, точным `total`, категорией без трат (`"0"`) и сравнением периодов двумя вызовами (`amount_before`, `delta`, `total_before`); юнит-тесты, включая сценарий «Няня, Продукты, Кафе»; README; проверка — `ctest`
  Категория без трат — `"0.00"`, а не `"0"`: все суммы по precision валюты (D11). Без `categories` `total` — `total_eq` из SQL; сравнение без `categories` — объединение категорий обоих периодов.
- [x] 6.3 `income_expense_by_month` и `balance` (весь список и один кошелёк, «конец дня»); юнит-тесты; README; проверка — `ctest`
- [x] 6.4 `search_operations`: фильтры `text`/`categories`/`wallets`/`kind` (`expense` по умолчанию, `any` — без фильтра типа, сальдо; эхо `kind`; описание инструмента объясняет сальдо при `any`), `limit` 1..50 (по умолчанию 20), `has_more`, `total`/`incomplete` только при фильтре, комментарий ≤120 байт; на `400` wallet-report до выката фильтров — `isError` «поиск пока недоступен»; юнит-тесты; README; проверка — `ctest`
  Плюс `count` (`total_count`) и `currency`. Запасной ответ «поиск пока недоступен» не делался: фильтры wallet-report (`add-operations-filters`, 0.1.1) уже выкачены в test и архивированы, а wallet-mcp выкатывается после wallet-report. К тому же wallet-report 0.1.0 на неизвестные параметры отвечает не `400`, а `200` без фильтрации — поэтому ответ без `total_eq` при отправленном фильтре считается «Отчёты временно недоступны», а не молча отдаёт неотфильтрованные строки.

## 7. Сквозная проверка

- [x] 7.1 На живом стеке (test-контур) через гейтвей: без токена → `401`; с MCP-токеном из веб-кабинета `initialize` → `tools/list` → каждый из шести инструментов на реальных данных; суммы `spending_by_category` совпадают с экраном «Категории» в вебе за тот же период; в логах нет сумм и комментариев; проверка — curl-скрипт в `scripts/smoke.sh` и просмотр логов
  Итог 2026-11-02, test через :8091: без токена и с поддельным wmcp_ — 401 от wallet-mcp; с токеном из веба initialize (2025-11-25, Mcp-Session-Id) → tools/list (6) → все шесть инструментов на реальных данных, ошибки аргументов — -32602 с текстом; spending_by_category за 2026-09 совпадает с /api/reports/expense-structure (источник экрана «Категории») — итог и все 26 категорий, EUR; логи wallet-mcp — только события и коды, без сумм, текста поиска и токена. Найдено вне сервиса: гейтвей в debug пишет тела запросов и ответов /api/mcp/ (см. wallet-gateway add-mcp-route 3.1).
  `scripts/smoke.sh` написан и прогнан на изолированном стенде (заглушки google-auth и wallet-report на Python, прямо в сервис, без гейтвея): все проверки ok. Открыто: test-контур через гейтвей — нужны выкат google-auth#2, wallet-gateway `add-mcp-route`, wallet-infra `add-wallet-mcp-service` и токен из веба.
- [x] 7.2 Отзыв: отозвать MCP-сессию в «Моих устройствах» и убедиться, что не позже чем через 5 минут вызовы получают `401`; проверка — curl — Тест: [wallet-mcp#2](https://github.com/WalletTransactions/wallet-mcp/issues/2) — проверено 2026-11-02: 401 через 271 с после отзыва
  На стенде: после отзыва в заглушке JWT из кеша ещё работает (200), с пустым кешем (рестарт) — 401. Истечение кеша — юнит-тест. Открыто: на test-контуре.
- [ ] 7.3 Ручная проверка из LM Studio: в `mcp.json` сервер с `url` `http://<хост>:8090/api/mcp/` и заголовком `Authorization: Bearer wmcp_…`; LM Studio видит шесть инструментов, вопрос «сколько я потратил на няню и еду в сентябре?» приводит к вызову `spending_by_category` с верным периодом и ответу с суммой из `total`; записать версию протокола, которую прислала LM Studio, и итоги в README (раздел «Подключение LM Studio») — Тест: [wallet-mcp#3](https://github.com/WalletTransactions/wallet-mcp/issues/3)

## Workflow follow-up

- Убрать запасной ответ «поиск пока недоступен» после выката `wallet-report/add-operations-filters`. — Не понадобилось: запасной ответ не делался (см. 6.4).
- Архивировать change после выката в prod и проверки 7.1–7.3.

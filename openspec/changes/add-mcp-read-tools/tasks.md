# Tasks

## 1. Репозиторий и каркас

- [ ] 1.1 Завести репозиторий: `git init` в `wallet-mcp/`, `.gitignore` и `.dockerignore` по образцу wallet-report, `VERSION` (`0.1.0`), `README.md` (назначение, переменные окружения, пустая таблица инструментов). Создать `WalletTransactions/wallet-mcp` в GitHub-организации и запушить; проверка — `git remote -v` и первый коммит виден в GitHub
- [ ] 1.2 Вендорить зависимости: `third_party/httplib.h`, `third_party/nlohmann/`, `third_party/swagger-ui/` (как в wallet-report) и `third_party/wallet-common/` через `wallet-common/scripts/vendor.sh`; проверка — `vendor.sh --check` проходит
- [ ] 1.3 `CMakeLists.txt` и `src/version.hpp.in` по образцу wallet-report, без libpq; цели `wallet_mcp` и `wallet_mcp_tests` (ctest); проверка — `docker build` собирает пустой `main`, `ctest` запускается (0 тестов)
- [ ] 1.4 `Containerfile` (многостадийный, `BUILD_JOBS=3`, runtime с `tzdata` и `curl` для healthcheck, пользователь `appuser`); проверка — образ собирается, контейнер стартует и отвечает на `/healthz`
- [ ] 1.5 GitHub Actions `ci.yml`, `release.yml`, `packages-cleanup.yml` по образцу wallet-report (образ `ghcr.io/wallettransactions/wallet-mcp`); проверка — CI зелёный на первом push, тег `v0.1.0-rc.1` публикует образ

## 2. Служебные эндпоинты (`service-endpoints`)

- [ ] 2.1 `main.cpp`: `.env`, обязательные `AUTH_SERVICE_URL` и `WALLET_REPORT_URL` с текстом `<KEY> must be set — …`, `PORT` (8088), `MCP_TZ` (UTC, неизвестный пояс — выход с кодом 1), JSON-логи с `trace_id` из `X-Trace-Id`; проверка — запуск без переменной и с `MCP_TZ=Mars/Olympus` завершается кодом 1, с ними — слушает порт
- [ ] 2.2 `docs.cpp`: `/healthz`, `/version`, `/openapi.yaml`, `/docs` (вендоренный Swagger UI), `/metrics` с `build_info` и гистограммой HTTP; `/healthz` и `/metrics` не пишутся в access-лог; проверка — curl каждого пути
- [ ] 2.3 `openapi.yaml` сервиса: `POST /api/mcp/` (JSON-RPC, ответы `200`/`202`/`401`/`413`/`503`), `GET /api/mcp/` → `405`, служебные пути; README — раздел «Эндпоинты»; проверка — `/docs` показывает спецификацию

## 3. Доступ (`mcp-access`)

- [ ] 3.1 Клиент google-auth `POST /internal/mcp/token` (таймауты 3 с/5 с, исходы: `ok`/`invalid`/`unavailable`); юнит-тест на фейковом HTTP-сервере (200, 401, 500, таймаут); проверка — `ctest`
- [ ] 3.2 Кеш JWT по SHA-256 токена до `expires_in − 30 с`, без хранения самого токена, с метриками `mcp_token_exchange_total` и `mcp_token_cache_total`; юнит-тесты: попадание, промах, истечение, сброс по `401` wallet-report и один повтор; проверка — `ctest`
- [ ] 3.3 Проверка каждого запроса до разбора JSON-RPC: нет заголовка или нет префикса `wmcp_` → `401` с `{"error":"invalid_token","message":…}` и `WWW-Authenticate: Bearer`; google-auth недоступен → `503`; событие `mcp_auth_rejected` без токена и хэша; проверка — юнит-тесты на все ветки и curl без токена на живом стеке

## 4. Транспорт MCP (`mcp-transport`)

- [ ] 4.1 Лимит тела 64 КБ → `413` до аутентификации; `GET /api/mcp/` → `405`; проверка — curl
- [ ] 4.2 Диспетчер JSON-RPC: `-32700`, `-32600` (включая батч), `-32601`, `ping`, уведомления → `202` без тела; юнит-тесты на каждый код; проверка — `ctest`
- [ ] 4.3 `initialize`: выбор версии протокола (D1), `capabilities`, `serverInfo`, `instructions` с датой в `MCP_TZ` и правилами, новый `Mcp-Session-Id` (UUID v4) без хранения; сверить с актуальной спецификацией MCP версии и заголовок `MCP-Protocol-Version`; юнит-тест на текст `instructions` с фиксированной датой; проверка — `ctest`
- [ ] 4.4 `tools/list` и `tools/call` с реестром инструментов: проверка аргументов по схеме (`-32602` на лишние ключи, неверные типы, неизвестный инструмент), `content[0].text` + `isError`; юнит-тест, что `as_user_id` и `user_id` отклоняются; проверка — `ctest`

## 5. Общие части инструментов (`wallet-tools`)

- [ ] 5.1 `src/decimal.*`: разбор, сложение, вычитание и вывод десятичных строк на `__int128` с масштабом 10^8 (D10); юнит-тесты: `0.10 + 0.20 + 1999.99 = 2000.29`, отрицательные, 12 знаков целой части, мусор во входе; проверка — `ctest`
- [ ] 5.2 Даты: разбор `ГГГГ-ММ-ДД` с проверкой существования, `from ≤ to`, перевод в границы wallet-report и «конец дня» для `/balance` (D6), «сегодня» в `MCP_TZ`; юнит-тесты (29 февраля, 31 апреля, смена года); проверка — `ctest`
- [ ] 5.3 Справочник счетов через `reports_client` и `parse_accounts`, кеш 60 с по хэшу токена, повтор поиска при ненайденном имени, валюта по умолчанию — первый кошелёк, `currency` проверяется по справочнику валют (D8, D9); тексты «подходит к нескольким» и «не найдено» через `resolve_name`; юнит-тесты на фейковом `ReportsApi`; проверка — `ctest`
- [ ] 5.4 Ответ wallet-report не `200`/`401` или сеть → `isError` «Отчёты временно недоступны, попробуйте позже.», метрики `mcp_tool_calls_total`/`mcp_tool_duration_seconds`, лог вызова без аргументов и сумм; юнит-тест, что в лог не попадает текст `text`; проверка — `ctest`

## 6. Инструменты (`wallet-tools`)

- [ ] 6.1 `list_wallets` и `list_categories`; юнит-тесты на фейковом `ReportsApi` (системные категории скрыты, итог и `incomplete`); строки в таблице инструментов README; проверка — `ctest`
- [ ] 6.2 `spending_by_category` с фильтром категорий, точным `total`, категорией без трат (`"0"`) и сравнением периодов двумя вызовами (`amount_before`, `delta`, `total_before`); юнит-тесты, включая сценарий «Няня, Продукты, Кафе»; README; проверка — `ctest`
- [ ] 6.3 `income_expense_by_month` и `balance` (весь список и один кошелёк, «конец дня»); юнит-тесты; README; проверка — `ctest`
- [ ] 6.4 `search_operations`: фильтры `text`/`categories`/`wallets`/`kind` (`expense` по умолчанию, `any` — без фильтра типа, сальдо; эхо `kind`; описание инструмента объясняет сальдо при `any`), `limit` 1..50 (по умолчанию 20), `has_more`, `total`/`incomplete` только при фильтре, комментарий ≤120 байт; на `400` wallet-report до выката фильтров — `isError` «поиск пока недоступен»; юнит-тесты; README; проверка — `ctest`

## 7. Сквозная проверка

- [ ] 7.1 На живом стеке (test-контур) через гейтвей: без токена → `401`; с MCP-токеном из веб-кабинета `initialize` → `tools/list` → каждый из шести инструментов на реальных данных; суммы `spending_by_category` совпадают с экраном «Категории» в вебе за тот же период; в логах нет сумм и комментариев; проверка — curl-скрипт в `scripts/smoke.sh` и просмотр логов
- [ ] 7.2 Отзыв: отозвать MCP-сессию в «Моих устройствах» и убедиться, что не позже чем через 5 минут вызовы получают `401`; проверка — curl
- [ ] 7.3 Ручная проверка из LM Studio: в `mcp.json` сервер с `url` `http://<хост>:8090/api/mcp/` и заголовком `Authorization: Bearer wmcp_…`; LM Studio видит шесть инструментов, вопрос «сколько я потратил на няню и еду в сентябре?» приводит к вызову `spending_by_category` с верным периодом и ответу с суммой из `total`; записать версию протокола, которую прислала LM Studio, и итоги в README (раздел «Подключение LM Studio»)

## Workflow follow-up

- Убрать запасной ответ «поиск пока недоступен» после выката `wallet-report/add-operations-filters`.
- Архивировать change после выката в prod и проверки 7.1–7.3.

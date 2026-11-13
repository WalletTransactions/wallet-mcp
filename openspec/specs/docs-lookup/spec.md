# docs-lookup Specification

## Purpose
Инструмент `ask_docs`: модель MCP-клиента задаёт вопрос о том, как устроены система и
приложение, и получает ответ по OpenSpec-спекам сервиса `wallet-docs` с указанием
разделов-источников. Данных кошелька инструмент не касается; суммы по-прежнему считает
SQL в wallet-report.

## Requirements

### Requirement: Инструмент появляется только при подключённом wallet-docs
Если `WALLET_DOCS_URL` задан, `tools/list` SHALL содержать инструмент `ask_docs`. Если
пуст, инструмента SHALL NOT быть в `tools/list`, а `tools/call` с этим именем — отвечать
JSON-RPC `-32602` (неизвестный инструмент), как для любого несуществующего. Остальные
инструменты и приём токенов от наличия `WALLET_DOCS_URL` не зависят.

#### Scenario: wallet-docs не подключён
- **WHEN** `WALLET_DOCS_URL` пуст, клиент вызывает `tools/list`
- **THEN** в ответе семь инструментов без `ask_docs`

#### Scenario: wallet-docs подключён
- **WHEN** `WALLET_DOCS_URL=http://wallet-docs:8089`, клиент вызывает `tools/list`
- **THEN** в ответе восемь инструментов, среди них `ask_docs`

#### Scenario: Вызов отсутствующего инструмента
- **WHEN** `WALLET_DOCS_URL` пуст, клиент вызывает `tools/call` с `name: "ask_docs"`
- **THEN** ответ JSON-RPC `-32602`, wallet-docs не вызывается

### Requirement: Аргумент и описание
`ask_docs` SHALL принимать единственный аргумент `question` — строку не длиннее 1000
символов (`maxLength` в `inputSchema`, `additionalProperties: false`). Нарушение схемы
(нет аргумента, не строка, длиннее 1000) — JSON-RPC `-32602`. Строка из одних пробелов —
`isError: true` с текстом «Вопрос пустой.»; wallet-docs не вызывается.

#### Scenario: Пустой вопрос
- **WHEN** `question: "   "`
- **THEN** `isError: true`, текст «Вопрос пустой.», wallet-docs не вызывается

#### Scenario: Слишком длинный вопрос
- **WHEN** `question` длиннее 1000 символов
- **THEN** JSON-RPC `-32602`, wallet-docs не вызывается

### Requirement: Описание для модели
Описание `ask_docs` на русском SHALL говорить, что инструмент отвечает на вопросы «как
работает система, приложение или MCP» по описанию системы, и прямо указывать, что цифры по
тратам, балансам и операциям он не отдаёт — для них есть другие инструменты.

#### Scenario: Модель выбирает инструмент
- **WHEN** клиент читает `tools/list`
- **THEN** описание `ask_docs` содержит, что цифры по тратам и балансам он не отдаёт

### Requirement: Вид ответа
Успешный вызов SHALL вернуть в `content` один текстовый элемент с JSON
`{"answer": "<текст>", "sources": [{"repo","capability","heading"}]}`: `answer` и
`sources` — ровно то, что вернул wallet-docs (`POST /api/docs/ask`, контракт — `openapi.yaml`
wallet-docs), без правки текста и без добавления собственных источников. Ответ «В описании
системы этого нет.» с пустым `sources` — успешный результат, не ошибка.

#### Scenario: Вопрос по теме
- **WHEN** модель спрашивает «Как строится баланс на дату?», wallet-docs отвечает текстом и двумя источниками
- **THEN** `isError` не задан, JSON в `content` содержит этот `answer` и оба источника

#### Scenario: В описании этого нет
- **WHEN** wallet-docs отвечает `{"answer":"В описании системы этого нет.","sources":[]}`
- **THEN** `isError` не задан, `sources` пуст

### Requirement: JWT для wallet-docs
Сервис SHALL вызывать wallet-docs только JWT от google-auth с `aud` = `wallet-docs` и
`scope` = `docs:ask`: для `wmcp_…` — `POST /internal/mcp/token` с `{"token","audience":"wallet-docs"}`,
для JWT — `POST /internal/token/exchange` (actor `wallet-mcp`, `audience=wallet-docs`;
контракты — `openapi.yaml` google-auth). Предъявленный клиентом токен в wallet-docs не
уходит.

#### Scenario: Первый вопрос с wmcp_
- **WHEN** запрос несёт действительный `wmcp_…`, кеша JWT для wallet-docs нет
- **THEN** сервис вызывает `/internal/mcp/token` с `audience: "wallet-docs"` и шлёт вопрос в wallet-docs с полученным `access_token`

#### Scenario: Вопрос из чата wallet-ai
- **WHEN** запрос несёт JWT для wallet-mcp, вопрос идёт в wallet-docs
- **THEN** сервис вызывает `/internal/token/exchange` с `audience=wallet-docs` и `X-Service-Secret` из `AUTH_EXCHANGE_SECRET`

### Requirement: Кеш JWT для wallet-docs
JWT для wallet-docs SHALL кешироваться в памяти по SHA-256 предъявленного токена по тому
же правилу, что JWT для wallet-report (запас до `exp` — 30 с), но отдельно от него. JWT
для wallet-docs SHALL NOT использоваться для вызовов wallet-report и наоборот.

#### Scenario: Серия вопросов
- **WHEN** модель задаёт три вопроса за минуту одним токеном
- **THEN** обмен для wallet-docs выполнен один раз

#### Scenario: Кеши не пересекаются
- **WHEN** модель вызывает `balance` и `ask_docs` одним токеном
- **THEN** выполнены два обмена, и каждый JWT ушёл только к своему адресату

### Requirement: Область определяет wallet-docs
Какие разделы увидит пользователь, SHALL решать wallet-docs по проверенному JWT
(`wmcp_…` — `platform` = `mcp`: описание web-клиента и самого MCP, без внутренностей
бэкенда; JWT из чата — область `platform` исходной сессии). У
инструмента SHALL NOT быть аргументов, расширяющих или задающих область (`repo`, `kind`,
`role`, `platform`).

#### Scenario: Вопрос про устройство гейтвея
- **WHEN** MCP-клиент спрашивает «Как гейтвей проверяет JWT?»
- **THEN** wallet-docs отвечает «В описании системы этого нет.» по правилам своей области, wallet-mcp ответ не меняет

### Requirement: Отказы и недоступность
Ошибки wallet-docs и обмена SHALL превращаться в результат инструмента (`isError: true`),
а не в HTTP-ошибку MCP-запроса, чтобы остальные инструменты продолжали работать. Тексты
(дословно): «Слишком много вопросов, повторите через <N> с.», «Справка временно
недоступна, попробуйте позже.», «Справка недоступна для этого токена.», «Вопрос не
принят: <причина>.». Отказ входящего токена по-прежнему даёт HTTP `401` до вызова
инструмента, как в `mcp-access`.

#### Scenario: Лимит вопросов
- **WHEN** wallet-docs отвечает `429` с `Retry-After: 54`
- **THEN** `isError: true`, текст «Слишком много вопросов, повторите через 54 с.»

#### Scenario: Модель ответов выключена
- **WHEN** wallet-docs отвечает `503` `{"error":"llm_unavailable"}`
- **THEN** `isError: true`, текст «Справка временно недоступна, попробуйте позже.», остальные инструменты работают

#### Scenario: Ключ google-auth сменился
- **WHEN** закешированный JWT для wallet-docs подписан старым ключом и wallet-docs отвечает `401`
- **THEN** сервис меняет токен заново и повторяет вызов один раз

#### Scenario: wallet-docs без прав
- **WHEN** wallet-docs отвечает `403` (платформа не в таблице области) или повторно `401`
- **THEN** `isError: true`, текст «Справка недоступна для этого токена.»

#### Scenario: Сеть и таймаут
- **WHEN** wallet-docs не отвечает за 25 с или соединение не установлено
- **THEN** `isError: true`, текст «Справка временно недоступна, попробуйте позже.»

#### Scenario: Вопрос не принят
- **WHEN** wallet-docs отвечает `400` с причиной
- **THEN** `isError: true`, текст «Вопрос не принят: <причина>.»

#### Scenario: Обмен для wallet-docs отклонён
- **WHEN** google-auth отвечает `400` или `401` на обмен для wallet-docs, либо не отвечает
- **THEN** `isError: true`, текст «Справка недоступна для этого токена.» при отказе и «Справка временно недоступна, попробуйте позже.» при недоступности google-auth

### Requirement: Журнал и метрики без текста вопросов
На вызов `ask_docs` SHALL писаться строка `mcp_tool_call` с теми же полями, что у других
инструментов (`user_id`, `tool`, `result`, `credential`, `duration_ms`), и дополнительно
`sources` — число источников. Текст вопроса и ответа SHALL NOT попадать в журнал и метрики.

#### Scenario: Успешный вопрос
- **WHEN** вызов `ask_docs` завершился ответом с тремя источниками
- **THEN** в журнале `tool: "ask_docs"`, `result: "ok"`, `sources: 3`, текста вопроса и ответа нет

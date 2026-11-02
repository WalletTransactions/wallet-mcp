# Spec Delta

## Purpose

MCP-сервер стека по транспорту Streamable HTTP: через один эндпоинт за
гейтвеем MCP-клиент (LM Studio, позже любой) узнаёт список инструментов
кошелька и вызывает их по JSON-RPC.

## ADDED Requirements

### Requirement: Один эндпоинт JSON-RPC
Сервис SHALL принимать сообщения MCP методом `POST /api/mcp/` с телом
JSON-RPC 2.0 (`Content-Type: application/json`) и отвечать
`application/json` одним JSON-RPC-ответом на запрос. Потоковых ответов (SSE)
сервис не отдаёт: `GET /api/mcp/` отвечает `405`. Путь без завершающего `/`
сервис не обслуживает.

#### Scenario: Список инструментов
- **WHEN** аутентифицированный клиент шлёт `POST /api/mcp/` с `{"jsonrpc":"2.0","id":1,"method":"tools/list"}`
- **THEN** ответ `200` `application/json` с `{"jsonrpc":"2.0","id":1,"result":{"tools":[...]}}`

#### Scenario: Попытка открыть поток
- **WHEN** клиент шлёт `GET /api/mcp/` с `Accept: text/event-stream`
- **THEN** ответ `405`, поток не открывается

### Requirement: Рукопожатие initialize
На `initialize` сервис SHALL отвечать `result` с `protocolVersion`,
`capabilities` (`{"tools":{}}`), `serverInfo` (`name`: `wallet-mcp`,
`version` из `VERSION`) и `instructions` и выдавать заголовок
`Mcp-Session-Id`. Поддерживаемая версия протокола — та, что запросил клиент,
если сервис её знает, иначе последняя известная сервису.

#### Scenario: Первое подключение
- **WHEN** клиент шлёт `initialize` с поддерживаемой `protocolVersion`
- **THEN** `result.protocolVersion` равен запрошенной, `result.serverInfo.name` — `wallet-mcp`, в ответе есть заголовок `Mcp-Session-Id`

#### Scenario: Неизвестная версия протокола
- **WHEN** клиент запрашивает версию протокола, которую сервис не знает
- **THEN** `result.protocolVersion` — последняя версия, которую поддерживает сервис

### Requirement: Инструкции для модели
`instructions` в ответе `initialize` SHALL содержать сегодняшнюю дату
(`ГГГГ-ММ-ДД`, день недели) в поясе сервиса и правила: суммы не считать
самому, а брать из инструментов; периоды передавать явными датами;
неоднозначное имя переспрашивать у пользователя.

#### Scenario: Дата в инструкциях
- **WHEN** клиент подключается 2026-10-31 в поясе сервиса
- **THEN** `instructions` содержит `2026-10-31` и строку «Не считай суммы сам — используй результаты инструментов.»

### Requirement: Уведомления
Сообщения JSON-RPC без `id` (уведомления, в том числе
`notifications/initialized` и `notifications/cancelled`) сервис SHALL
принимать и отвечать `202` без тела.

#### Scenario: После рукопожатия
- **WHEN** клиент шлёт `{"jsonrpc":"2.0","method":"notifications/initialized"}`
- **THEN** ответ `202` с пустым телом

### Requirement: Сессия не подтверждает личность
Заголовок `Mcp-Session-Id` SHALL NOT использоваться для аутентификации:
каждый запрос проверяется по своему `Authorization` (см. `mcp-access`).
Запрос без `Mcp-Session-Id` или с неизвестным сервису значением
обслуживается, если токен действителен. Состояние сессий сервис не хранит.

#### Scenario: Чужой Mcp-Session-Id
- **WHEN** запрос несёт `Mcp-Session-Id`, выданный другому пользователю, и свой действительный MCP-токен
- **THEN** данные возвращаются по токену запроса, а не по сессии

#### Scenario: Перезапуск сервиса
- **WHEN** после рестарта сервиса клиент продолжает слать старый `Mcp-Session-Id`
- **THEN** запросы обслуживаются без повторного `initialize`

### Requirement: Ошибки протокола
Сервис SHALL отвечать JSON-RPC-ошибкой с HTTP `200`: тело не JSON — `-32700`;
не объект JSON-RPC 2.0 — `-32600`; неизвестный метод — `-32601`;
`tools/call` с неизвестным инструментом или аргументами не по схеме —
`-32602`. Батчи (JSON-массив) не поддерживаются, на них приходит `-32600`.
`ping` отвечает пустым `result`.

#### Scenario: Неизвестный метод
- **WHEN** клиент шлёт `{"jsonrpc":"2.0","id":7,"method":"resources/list"}`
- **THEN** ответ `{"jsonrpc":"2.0","id":7,"error":{"code":-32601,...}}`

#### Scenario: Неизвестный инструмент
- **WHEN** `tools/call` с `name: "delete_operation"`
- **THEN** ошибка `-32602`, wallet-report не вызывается

#### Scenario: Батч
- **WHEN** тело — JSON-массив из двух запросов
- **THEN** ошибка `-32600`

### Requirement: Результат инструмента
`tools/call` SHALL возвращать `result.content` из одного элемента
`{"type":"text","text":<компактный JSON результата>}` и `isError: false`.
Ошибка уровня инструмента (имя не найдено, неоднозначно, wallet-report
отказал) возвращается как `result` с `isError: true` и текстом для модели, а
не как JSON-RPC-ошибка.

#### Scenario: Ненайденная категория
- **WHEN** модель вызывает `spending_by_category` с категорией, которой нет
- **THEN** `result.isError` — `true`, текст называет категорию и перечисляет существующие

### Requirement: Лимит тела запроса
Сервис SHALL отклонять тело запроса больше 64 КБ ответом `413`, не разбирая
его.

#### Scenario: Огромный запрос
- **WHEN** клиент шлёт тело в 1 МБ
- **THEN** ответ `413`, обмен токена и вызовы wallet-report не выполняются

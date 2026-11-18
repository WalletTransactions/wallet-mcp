# Design

## Context

- `search_operations` (`src/tools.cpp`) делает один запрос
  `GET /api/reports/operations` и отдаёт страницу строк плюс `total`, `count`,
  `incomplete` из `total_eq`, `total_count`, `total_incomplete`. Других агрегатов
  в ответе wallet-report нет: ни дат, ни разбивки по категориям.
- Инвариант проекта: суммы считает SQL в wallet-report, а не модель и не wallet-mcp
  по сырым строкам. Страница ограничена `limit` (≤ 50), так что по ней нельзя
  честно ни найти самую раннюю операцию, ни разбить итог по категориям.
- У wallet-mcp есть прецедент вычисления производной величины: `avg_per_month`
  в `spending_by_category` делит уже посчитанный SQL итог на `months` (`average()`).
  `avg_interval_days` устроена так же: деление двух значений из wallet-report.
- Фильтр `q` у wallet-report — подстрока комментария по ИЛИ, до 10 вариантов,
  `ё` = `е`, `%` и `_` буквально. `exclude_q` должен вести себя так же, но с НЕ.

## Goals / Non-Goals

**Goals:**
- Один вызов `search_operations` отвечает на «сколько на X, когда первый и последний
  раз, как часто, в каких категориях лежит X».
- Ложные срабатывания по тексту отсекаются без привязки к категории (`exclude_text`).
- Совпадения по тексту в чужой категории не теряются молча (`other_matches`).
- Старые клиенты wallet-report-API (`wallet-web`, `android-wallet`) не меняются.

**Non-Goals:**
- Группировка по месяцам/кошелькам (`group_by`), группы и алиасы категорий,
  режим `last`/`first`, сохранённые фильтры — отдельные предложения.
- `exclude_text` для `spending_by_category*` — только `search_operations`.
- `other_matches` для `exclude_categories`: исключённые категории не показываются
  (пользователь сам их назвал). Только при `text` + `categories`.
- Правки в `wallet-common`, `google-auth`, `wallet-gateway`, `wallet-api`, `wallet-infra`.

## Decisions

### D1. Сводку считает wallet-report, не wallet-mcp
Новая необязательная пара параметров у `GET /api/reports/operations`:
`summary=true` и повторяемый `exclude_q` (до 10, по ≤ 100 символов).

При `summary=true` и хотя бы одном фильтре ответ содержит:
```
summary: {first_date, last_date,
          by_category: [{category_uuid, category_name, total_eq, count}]}
other_matches: [{category_uuid, category_name, total_eq, count}]   // только при q + category_uuid
```
`first_date`/`last_date` — `MIN/MAX(date)`, `by_category` — `GROUP BY` по тому же
отфильтрованному набору, что и `total_eq`, — одна и та же выборка, поэтому сумма
`by_category` равна `total_eq` (кроме операций без курса — они в `total_eq` не входят,
а в `count` входят; это уже описано у `total_incomplete`). `other_matches` — тот же
запрос с `q`, но без `category_uuid` и с условием `category NOT IN (…)`.

Почему не считать в wallet-mcp: пришлось бы пролистать все страницы и сложить строки
— нарушение инварианта, медленно и непредсказуемо при больших выборках.
Почему opt-in (`summary=true`): без параметра ответ прежний, «Без фильтров ответ прежний,
итога нет и второго запроса к базе тоже» (`openapi.yaml` wallet-report) сохраняется,
`wallet-web` и `android-wallet` не платят за лишний SQL.

**Правка в другом сервисе — wallet-report** (сопутствующий change, см. tasks.md §1):
`openapi.yaml`, хендлер и SQL `/api/reports/operations`, тесты. Остальные сервисы не меняются.

### D2. `avg_interval_days` считает wallet-mcp
`(last_date − first_date) / (count − 1)` в днях, один знак после запятой, только при
`count ≥ 2`. Это деление двух значений из ответа wallet-report (как `avg_per_month`),
а не суммирование сырых строк. `count` — число операций, а не число различных дней:
две стрижки в один день дадут интервал 0 — приемлемо, так «между операциями» и
называется. Формат — строка с одним знаком после запятой (`"123.5"`), как суммы:
число без потери точности, и модель не округляет сама.

### D3. `exclude_text` — тот же разбор, что у `text`
Валидация общая с `text` (массив строк, 1..10, ≤ 100 символов, trim, пустые и повторы
отбрасываются; строка вместо массива — `-32602`). Уходит повторённым `exclude_q`.
В wallet-report условие — `NOT (comment ILIKE ANY(...))` с теми же правилами
нормализации; операция с пустым комментарием не исключается.
`exclude_text` считается фильтром: при нём в ответе есть `total` и `summary`.

### D4. `other_matches` — только при `text` + `categories`
Без `text` вопрос «что ещё совпало» не имеет смысла, без `categories` совпавшие
категории и так видны в `summary.by_category`. `[]` вместо отсутствия поля — чтобы
модель отличала «проверено, пусто» от «не проверялось». `other_matches` учитывает
`exclude_text`, `wallets`, `kind`, период; не учитывает `categories` (по определению).

### D5. Совместимость при выкатке
Порядок: wallet-report, потом wallet-mcp. Если wallet-mcp новее wallet-report, а
`summary` в ответе нет, блок молча опускается (спека «Устаревший wallet-report без
сводки»), `exclude_text` при этом не может «молча не сработать»: старый wallet-report
игнорирует неизвестный `exclude_q`, и модель получила бы неотфильтрованный итог без
предупреждения. Поэтому wallet-mcp, получив `exclude_text`, требует от ответа признак
поддержки: wallet-report при `exclude_q` обязан вернуть `summary` (даже при `count: 0`);
его отсутствие при заданном `exclude_text` — `ReportsDown()` («Отчёты временно недоступны»),
а не неверная сумма. Для `summary`/`other_matches` без `exclude_text` отсутствие — не ошибка.

### D6. Описание инструмента
В `tools/list` описание `search_operations` дополняется одним абзацем: что такое
`exclude_text`, что `summary` и `other_matches` посчитаны по всем подходящим операциям
периода, а не по странице, и что при непустом `other_matches` стоит сказать пользователю
про совпадения в других категориях. Тексты — на русском, дословно в спеке и README.

## Risks / Trade-offs

- **Лишний SQL.** `summary` — ещё один агрегирующий запрос (и третий для
  `other_matches`). Mitigation: opt-in, только при фильтрах, тот же индекс по периоду;
  замерить на тестовой базе (tasks §1.4).
- **Неточность `by_category` при операциях без курса.** `total_eq` не включает их,
  `count` включает; `incomplete` уже сообщает об этом. Новых флагов не вводим.
- **Размер ответа.** Много категорий в `by_category` — ограничить 20 строками по убыванию
  `total` (остальное — строка «прочее» не вводим, достаточно `count` и `total` общего).
  Решение: top-20, при обрезании — `by_category_truncated: true`. Если окажется лишним —
  убрать, при 20 категориях на одну подстроку это крайний случай.
- **Старый клиент после отката wallet-report.** Покрыто D5.

## Open Questions

- Нужен ли `other_matches` для `exclude_categories` (показывать, что именно исключено)? Сейчас — нет.

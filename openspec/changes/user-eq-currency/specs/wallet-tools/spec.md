## MODIFIED Requirements

### Requirement: Валюта эквивалента
Инструменты с суммами SHALL принимать необязательный `currency` (ISO-код).
Если он не задан, используется первая подходящая из: `preferred_eq_currency`
справочника `GET /api/reports/currencies` (валюта, сохранённая пользователем);
`EUR`, если он есть в справочнике и не скрыт из эквивалента; валюта первого
кошелька из справочника `GET /api/reports/accounts`. Неизвестный код приводит к
`isError: true`. Если кошельков нет, возвращается `isError: true` с текстом
«Кошельков пока нет — заведите их в приложении.».

#### Scenario: Валюта, выбранная пользователем
- **WHEN** справочник валют отвечает `preferred_eq_currency: "RUB"`, первый кошелёк — «Albanian» (ALL), `currency` не задан
- **THEN** суммы считаются в RUB, и в ответе `currency: "RUB"`

#### Scenario: Ничего не выбрано, EUR есть
- **WHEN** `preferred_eq_currency: null`, первый кошелёк — «Albanian» (ALL), `EUR` в справочнике есть и не скрыт
- **THEN** суммы считаются в EUR

#### Scenario: Валюта по умолчанию
- **WHEN** `preferred_eq_currency` нет, `EUR` в справочнике нет, у пользователя кошельки «Наличные» (RUB) и «Revolut» (USD) в этом порядке справочника, `currency` не задан
- **THEN** суммы считаются в RUB, и в ответе `currency: "RUB"`

#### Scenario: Явная валюта главнее
- **WHEN** `preferred_eq_currency: "RUB"`, вызов с `currency: "EUR"`
- **THEN** суммы считаются в EUR

### Requirement: ledger_overview
`ledger_overview()` без аргументов SHALL вернуть, что есть в леджере и какое
сегодня число, чтобы модель не просила период у пользователя. Ответ содержит:
`today` (`ГГГГ-ММ-ДД` в поясе сервиса), `weekday` (по-русски, строчными),
`data_period` `{"from","to"}` — от даты первой операции до сегодня, но не длиннее
36 месяцев (при более длинной истории `from` сдвигается так, что период
укладывается в 36 месяцев и начинается с первого числа месяца), `operations_count`
(число живых операций за всё время), `wallets` (`name`, `currency` живых
неархивных кошельков в порядке справочника), `currencies` (ISO-коды без повторов),
`default_currency` (валюта по умолчанию из «Валюта эквивалента») и `hint`.
Источники — `GET /api/reports/data-range`, `GET /api/reports/accounts` и
`GET /api/reports/currencies` wallet-report; сервис
сам операций не читает и ничего не суммирует.

Если операций нет, `data_period` равен `null`, `operations_count` — `0`, а `hint`
— «Операций пока нет — заведите их в приложении.». Иначе `hint` — «Если
пользователь не назвал период, бери data_period. Суммы и средние бери из
инструментов, сам не считай.».

#### Scenario: Данные за год
- **WHEN** первая операция 2025-12-01, сегодня 2026-11-19, 475 операций, один кошелёк «Карта Visa» в EUR
- **THEN** `data_period` — `{"from":"2025-12-01","to":"2026-11-19"}`, `operations_count` — `475`, `default_currency` — `EUR`, `hint` содержит «бери data_period»

#### Scenario: Длинная история
- **WHEN** первая операция 2019-03-14, сегодня 2026-11-19
- **THEN** `data_period.from` — `2023-12-01`, `data_period.to` — `2026-11-19`, период не длиннее 36 месяцев

#### Scenario: Пустой леджер
- **WHEN** в леджере нет операций
- **THEN** `data_period` — `null`, `operations_count` — `0`, `hint` — «Операций пока нет — заведите их в приложении.», `isError` не выставляется

#### Scenario: Нет доступа к данным
- **WHEN** wallet-report не отвечает или возвращает ошибку
- **THEN** `isError: true` с текстом ошибки, как у остальных инструментов

#### Scenario: Выбранная валюта в обзоре
- **WHEN** справочник валют отвечает `preferred_eq_currency: "EUR"`, первый кошелёк — «Albanian» (ALL)
- **THEN** `default_currency` — `EUR`

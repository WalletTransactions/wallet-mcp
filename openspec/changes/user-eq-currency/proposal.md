# Proposal

## Why

Issue [.github-private#19](https://github.com/WalletTransactions/.github-private/issues/19). Без `currency` инструменты считают в
валюте первого кошелька справочника. Кошельки отсортированы по имени, и на
test в общем леджере первым оказался «Albanian»: чат ответил про расходы на
нянь в леках (197 410 ALL), евро пользователю пришлось просить отдельно.

Решение владельца: валюта эквивалента хранится в настройках пользователя на
сервере (wallet-api `user_settings`), веб её пишет, а wallet-report отдаёт
`preferred_eq_currency` в `/api/reports/currencies`. MCP берёт её по
умолчанию.

## What Changes

- Валюта по умолчанию (когда `currency` не передан) выбирается первым
  подходящим из: `preferred_eq_currency` справочника валют; `EUR`, если он
  есть в справочнике и не скрыт из эквивалента; валюта первого кошелька (как
  раньше).
- `ledger_overview.default_currency` — та же валюта.
- Явный `currency` в вызове по-прежнему главнее.

## Capabilities

### New Capabilities

Нет.

### Modified Capabilities

- `wallet-tools`: «Валюта эквивалента» и «ledger_overview» —
  новый порядок выбора валюты по умолчанию.

## Impact

- Код: `src/tools.cpp` (`Call::currency`, `ledger_overview`), разбор
  `/api/reports/currencies` (`directory`), `tests/test_tools.cpp`, README.
- Справочник валют кешируется 60 с на сессию: смена валюты в вебе доходит до
  MCP не позже чем через минуту.
- Зависит от wallet-report `user-eq-currency`. Со старым wallet-report поля
  нет, и порядок начинается с `EUR`.

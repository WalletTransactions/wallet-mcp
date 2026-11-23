## ADDED Requirements

### Requirement: "*" означает все
Если в `wallets`, `categories` или `exclude_categories` среди имён есть `"*"`
или `wallet` равен `"*"`, инструмент SHALL отвечать так, как будто этот
параметр не передан: без фильтра и без ошибки «не найдено». Другие имена в том
же массиве при этом не учитываются. Описания `wallets` и `categories` в
`tools/list` SHALL говорить, что для выбора всех параметр не передаётся.

#### Scenario: Все кошельки в поиске
- **WHEN** `search_operations` с `categories: ["Baby Sitter"]`, `wallets: ["*"]`
- **THEN** запрос в wallet-report уходит без `wallet_uuid`, ответ — операции «Baby Sitter» из всех кошельков, `isError` нет

#### Scenario: Все категории в расходе
- **WHEN** `spending_by_category` с `categories: ["*"]`
- **THEN** ответ такой же, как без `categories`: все категории периода и `total` из `total_eq`

#### Scenario: Звёздочка вместе с именем
- **WHEN** `wallets: ["*", "Cash"]`
- **THEN** фильтра по кошелькам нет

#### Scenario: Баланс по всем
- **WHEN** `balance` с `wallet: "*"`
- **THEN** ответ как без `wallet`: все кошельки и `total`

### Requirement: Совпадения вне фильтра кошельков
Если в `search_operations` задан `wallets`, курсора нет и подходящих операций
нет (`count` 0), ответ SHALL содержать `outside_wallets: {count, total}` по тем
же условиям во всех кошельках; значения считает wallet-report. `count: 0` —
«проверено, нет и в других кошельках». В остальных случаях поля нет.

#### Scenario: Операции лежат в другом кошельке
- **WHEN** `categories: ["Baby Sitter"]`, `wallets` — все кошельки, кроме Cash, а все 7 операций периода сделаны с Cash на 2 078.00
- **THEN** `count` 0, `operations` пуст, `outside_wallets` — `{"count": 7, "total": "2078.00"}`

#### Scenario: Нигде нет
- **WHEN** `categories: ["Baby Sitter"]`, `wallets: ["Cash"]`, операций по категории в периоде нет ни в одном кошельке
- **THEN** `outside_wallets` — `{"count": 0, "total": "0.00"}`

#### Scenario: Нашлось
- **WHEN** `wallets: ["Cash"]`, найдено 3 операции
- **THEN** поля `outside_wallets` нет, второй запрос не делается

#### Scenario: Без wallets
- **WHEN** `categories: ["Baby Sitter"]` без `wallets`, ничего не найдено
- **THEN** поля `outside_wallets` нет

#### Scenario: Второй запрос не удался
- **WHEN** `wallets: ["Cash"]`, ничего не найдено, а повторный запрос без кошельков wallet-report отклонил или вернул без итога
- **THEN** основной ответ возвращается без ошибки и без `outside_wallets`

#### Scenario: Следующая страница
- **WHEN** `wallets: ["Cash"]` и передан `cursor`
- **THEN** поля `outside_wallets` нет, второй запрос не делается

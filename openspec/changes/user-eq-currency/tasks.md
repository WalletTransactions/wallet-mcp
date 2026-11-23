## 1. Валюта по умолчанию

- [x] 1.1 Разбор `preferred_eq_currency` из `/api/reports/currencies` (кешируется вместе со справочником валют); `Call::currency` без `currency` — `preferred_eq_currency` → `EUR` (есть и не скрыт) → первый кошелёк. Проверка: тесты в `tests/test_tools.cpp` по сценариям спеки (выбрано, `null` + EUR, без EUR, явная валюта главнее, старый wallet-report без поля); `ctest` зелёный
- [x] 1.2 `ledger_overview.default_currency` — та же функция; README («по умолчанию берётся…», строка `ledger_overview`). Проверка: тест «Выбранная валюта в обзоре»

## 2. Выкат

- [ ] 2.1 rc на test после wallet-report `user-eq-currency`. Проверка: выбрать EUR в «Настройках» веба, в чате test спросить «сколько потрачено на нянь в этом месяце» — ответ в евро без уточнения

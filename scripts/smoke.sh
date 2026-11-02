#!/usr/bin/env bash
# Сквозная проверка wallet-mcp (задача 7.1 add-mcp-read-tools): без токена —
# 401, затем initialize → tools/list → каждый из шести инструментов.
#
#   MCP_TOKEN=wmcp_… bash scripts/smoke.sh http://<хост>:8090/api/mcp/ [from] [to]
#
# Адрес — через гейтвей (как у клиента) или прямо в сервис
# (http://127.0.0.1:8088/api/mcp/). Период по умолчанию — прошлый месяц.
# Печатает ответы инструментов: суммы spending_by_category сверить с экраном
# «Категории» веба за тот же период. Нужны curl и jq. Токен в вывод не попадает.
set -euo pipefail

url="${1:?адрес MCP, например http://127.0.0.1:8090/api/mcp/}"
: "${MCP_TOKEN:?MCP_TOKEN=wmcp_… — токен из веб-кабинета (Настройки → Устройства)}"
from="${2:-$(date -d "$(date +%Y-%m-01) -1 month" +%Y-%m-%d)}"
to="${3:-$(date -d "$(date +%Y-%m-01) -1 day" +%Y-%m-%d)}"
fail=0

rpc() { # rpc <json> → тело ответа; код — в $code
    local out
    out="$(curl -sS -o - -w '\n%{http_code}' -X POST "$url" \
        -H "Authorization: Bearer $MCP_TOKEN" -H 'Content-Type: application/json' \
        -H 'Accept: application/json, text/event-stream' -H 'MCP-Protocol-Version: 2025-06-18' \
        --data "$1")"
    code="${out##*$'\n'}"
    body="${out%$'\n'*}"
}

check() { # check <имя> <условие jq>
    if jq -e "$2" > /dev/null <<< "$body"; then
        echo "ok    $1"
    else
        echo "FAIL  $1 (HTTP $code): $body"
        fail=1
    fi
}

echo "== $url, период $from..$to"

code="$(curl -sS -o /dev/null -w '%{http_code}' -X POST "$url" -H 'Content-Type: application/json' \
    --data '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}')"
if [[ "$code" == "401" ]]; then echo "ok    без токена → 401"; else echo "FAIL  без токена → $code"; fail=1; fi

rpc '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"smoke","version":"1"}}}'
check initialize '.result.serverInfo.name == "wallet-mcp" and (.result.instructions | test("Не считай суммы сам"))'
jq -r '"      " + .result.protocolVersion + ": " + (.result.instructions | split("\n")[0])' <<< "$body"

rpc '{"jsonrpc":"2.0","method":"notifications/initialized"}'
if [[ "$code" == "202" ]]; then echo "ok    notifications/initialized → 202"; else echo "FAIL  notifications/initialized → $code"; fail=1; fi

rpc '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
check "tools/list: 6 инструментов" '.result.tools | length == 6'

call() { # call <id> <имя> <arguments json>
    rpc "{\"jsonrpc\":\"2.0\",\"id\":$1,\"method\":\"tools/call\",\"params\":{\"name\":\"$2\",\"arguments\":$3}}"
    check "$2" '.result.isError == false'
    jq -r '.result.content[0].text // .error.message' <<< "$body" | cut -c1-600 | sed 's/^/      /'
}

call 3 list_wallets '{}'
call 4 list_categories '{}'
call 5 spending_by_category "{\"from\":\"$from\",\"to\":\"$to\"}"
call 6 income_expense_by_month "{\"from\":\"$from\",\"to\":\"$to\"}"
call 7 balance '{}'
call 8 search_operations "{\"from\":\"$from\",\"to\":\"$to\",\"limit\":5}"

rpc '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"list_categories","arguments":{"as_user_id":"00000000-0000-4000-8000-000000000000"}}}'
check "as_user_id → -32602" '.error.code == -32602'

exit $fail

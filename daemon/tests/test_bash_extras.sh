#!/bin/bash
# Regression test for send_extras() in claude-usage-daemon.sh: the optional
# weather / stock writes. extras.py is stubbed (no network) and write_gatt is
# recorded instead of going over D-Bus.
#
# Invariants: nothing is sent until the config opts in; each payload is its own
# write; intervals are honoured; a failed fetch retries early; and extra writes
# never move LAST_WRITE_TS (the heartbeat's usage cadence).
set -u

DAEMON="$(dirname "$0")/../claude-usage-daemon.sh"

extract() { awk -v fn="$1" '$0 ~ "^"fn"\\(\\) \\{"{f=1} f{print} f&&/^\}/{exit}' "$DAEMON"; }
eval "$(extract read_config_string)"
eval "$(extract write_extra)"
eval "$(extract send_extras)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
CONFIG_FILE="$TMP/config"
EXTRAS_PY="$TMP/extras.py"
WRITES="$TMP/writes"
WEATHER_INTERVAL=900 STOCKS_INTERVAL=300 EXTRAS_RETRY=120
LAST_WEATHER=0 LAST_STOCKS=0 LAST_WRITE_TS=42 RX_CHAR_PATH=/rx

log() { :; }
sleep() { :; }
write_gatt() { echo "$2" >> "$WRITES"; }

# Stub fetcher: echoes canned payloads, or fails when $TMP/fail exists.
cat > "$EXTRAS_PY" <<EOF
import sys, os
if os.path.exists("$TMP/fail"): sys.exit(1)
if sys.argv[1] == "weather": print('{"k":"wx","loc":"%s","coords":"%s"}' % (sys.argv[2], sys.argv[3]))
else:
    for i, s in enumerate(sys.argv[2].split(",")): print('{"k":"q","i":%d,"sy":"%s"}' % (i, s.strip()))
EOF

fail=0
check() {  # check <label> <got> <want>
    if [ "$2" = "$3" ]; then echo "PASS: $1"; else echo "FAIL: $1: got '$2' want '$3'"; fail=1; fi
}
count() { [ -f "$WRITES" ] && wc -l < "$WRITES" | tr -d ' ' || echo 0; }

: > "$CONFIG_FILE"
send_extras
check "off by default: no writes" "$(count)" "0"

printf 'weather_location = Ennetmoos  # home\nweather_coords = 46.9, 8.3\nstocks = AMD, R3NK.DE=Renk\n' > "$CONFIG_FILE"
send_extras
check "weather + 2 quotes written" "$(count)" "3"
check "weather payload first, args passed" "$(head -1 "$WRITES")" '{"k":"wx","loc":"Ennetmoos","coords":"46.9, 8.3"}'
check "one write per quote" "$(tail -1 "$WRITES")" '{"k":"q","i":1,"sy":"R3NK.DE=Renk"}'
check "heartbeat cadence untouched" "$LAST_WRITE_TS" "42"

send_extras
check "intervals honoured: nothing new" "$(count)" "3"

touch "$TMP/fail"
LAST_WEATHER=0 LAST_STOCKS=0
now=$(date +%s)
send_extras
check "failure: no writes" "$(count)" "3"
check "weather retries after EXTRAS_RETRY" "$(( LAST_WEATHER + WEATHER_INTERVAL - now ))" "$EXTRAS_RETRY"
check "stocks retry after EXTRAS_RETRY" "$(( LAST_STOCKS + STOCKS_INTERVAL - now ))" "$EXTRAS_RETRY"

exit $fail

#!/bin/bash
# Regression test for send_extras() in claude-usage-daemon.sh: the extra-screen
# writes (pace, activity, service status, system, weather, stocks). extras.py is stubbed (no network) and write_gatt is
# recorded instead of going over D-Bus.
#
# Invariants: the opt-in screens send nothing until the config opts in, the
# default-on ones stop on `= off`; each payload is its own write; intervals are
# honoured; a failed fetch retries early; pace follows fresh polls only; and
# extra writes never move LAST_WRITE_TS (the heartbeat's usage cadence).
set -u

DAEMON="$(dirname "$0")/../claude-usage-daemon.sh"

extract() { awk -v fn="$1" '$0 ~ "^"fn"\\(\\) \\{"{f=1} f{print} f&&/^\}/{exit}' "$DAEMON"; }
eval "$(extract read_config_string)"
eval "$(extract read_onoff_setting)"
eval "$(extract read_default_on)"
eval "$(extract write_extra)"
eval "$(extract send_extras)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
CONFIG_FILE="$TMP/config"
EXTRAS_PY="$TMP/extras.py"
WRITES="$TMP/writes"
WEATHER_INTERVAL=900 STOCKS_INTERVAL=300 EXTRAS_RETRY=120
ACTIVITY_INTERVAL=30 STATUS_INTERVAL=300 SYSTEM_INTERVAL=30
LAST_WEATHER=0 LAST_STOCKS=0 LAST_WRITE_TS=42 RX_CHAR_PATH=/rx
LAST_ACTIVITY=0 LAST_STATUS=0 LAST_SYSTEM=0 LAST_HISTORY_TS=0
LAST_PAYLOAD="" LAST_PAYLOAD_TS=0 ALERT_SID=""
read_hour_format() { echo 24; }
read_config_dirs() { echo "/c1"; echo "/c2"; }

log() { :; }
sleep() { :; }
write_gatt() { echo "$2" >> "$WRITES"; }

# Stub fetcher: echoes canned payloads, or fails when $TMP/fail exists.
cat > "$EXTRAS_PY" <<EOF
import sys, os
if os.path.exists("$TMP/fail"): sys.exit(1)
k = sys.argv[1]
if k == "weather": print('{"k":"wx","loc":"%s","coords":"%s"}' % (sys.argv[2], sys.argv[3]))
elif k == "stocks":
    for i, s in enumerate(sys.argv[2].split(",")): print('{"k":"q","i":%d,"sy":"%s"}' % (i, s.strip()))
elif k == "history": print('{"k":"h","from":%s,"tf":%s}' % (sys.argv[2], sys.argv[3]))
elif k == "activity": print('{"k":"a","dirs":"%s","sid":"%s"}' % (sys.argv[2], sys.argv[3]))
elif k == "status": print('{"k":"ss"}')
elif k == "system": print('{"k":"sys"}')
EOF

fail=0
check() {  # check <label> <got> <want>
    if [ "$2" = "$3" ]; then echo "PASS: $1"; else echo "FAIL: $1: got '$2' want '$3'"; fail=1; fi
}
count() { [ -f "$WRITES" ] && wc -l < "$WRITES" | tr -d ' ' || echo 0; }

: > "$CONFIG_FILE"
printf 'activity = off\nservice_status = off\n' > "$CONFIG_FILE"
send_extras
check "opt-in screens off by default, default-on ones disabled: no writes" "$(count)" "0"

: > "$CONFIG_FILE"
ALERT_SID=abc
send_extras
check "activity + status on by default" "$(cat "$WRITES")" $'{"k":"a","dirs":"/c1,/c2","sid":"abc"}\n{"k":"ss"}'
: > "$WRITES"
ALERT_SID=""

LAST_PAYLOAD='{"s":5}' LAST_PAYLOAD_TS=100
send_extras
check "pace built from the last poll" "$(cat "$WRITES")" '{"k":"h","from":{"s":5},"tf":24}'
send_extras
check "pace waits for the next poll" "$(count)" "1"
: > "$WRITES"

printf 'activity = off\nservice_status = off\nweather_location = Ennetmoos  # home\nweather_coords = 46.9, 8.3\nstocks = AMD, R3NK.DE=Renk\nsystem_stats = on\n' > "$CONFIG_FILE"
send_extras
check "system + weather + 2 quotes written" "$(count)" "4"
check "system payload first" "$(head -1 "$WRITES")" '{"k":"sys"}'
check "weather payload, args passed" "$(sed -n 2p "$WRITES")" '{"k":"wx","loc":"Ennetmoos","coords":"46.9, 8.3"}'
check "one write per quote" "$(tail -1 "$WRITES")" '{"k":"q","i":1,"sy":"R3NK.DE=Renk"}'
check "heartbeat cadence untouched" "$LAST_WRITE_TS" "42"

send_extras
check "intervals honoured: nothing new" "$(count)" "4"

touch "$TMP/fail"
LAST_WEATHER=0 LAST_STOCKS=0 LAST_STATUS=0
printf 'weather_location = Ennetmoos\nstocks = AMD\nactivity = off\n' > "$CONFIG_FILE"
now=$(date +%s)
send_extras
check "failure: no writes" "$(count)" "4"
check "weather retries after EXTRAS_RETRY" "$(( LAST_WEATHER + WEATHER_INTERVAL - now ))" "$EXTRAS_RETRY"
check "stocks retry after EXTRAS_RETRY" "$(( LAST_STOCKS + STOCKS_INTERVAL - now ))" "$EXTRAS_RETRY"
check "status retries after EXTRAS_RETRY" "$(( LAST_STATUS + STATUS_INTERVAL - now ))" "$EXTRAS_RETRY"

exit $fail

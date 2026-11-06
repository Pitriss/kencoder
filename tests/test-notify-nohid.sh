#!/usr/bin/env bash
# Build KEncoder Notify and exercise config / playback with an isolated mock.
# No real encoder, installation or use of the existing user D-Bus session.
# A private --daemon test keeps KDE notification listening disabled.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p .log .tmp
log=.log/kencoder-notify-nohid-test.log
exec > >(tee "$log") 2>&1

printf '==> Build Qt6-only listener\n'
cmake -S notify -B .tmp/kencoder-notify-build -DCMAKE_BUILD_TYPE=Debug
cmake --build .tmp/kencoder-notify-build --parallel 2
bin=$PWD/.tmp/kencoder-notify-build/kencoder-notify
[[ -x "$bin" ]]

printf '==> Build both KCM plugins (no installation)\n'
cmake -S gui -B .tmp/kencoder-notify-gui-build -DKENCODER_REQUIRE_KCM=ON
cmake --build .tmp/kencoder-notify-gui-build --target kcm_kencoder kcm_kencoder_notify kencoder --parallel 2
printf '==> Headless GUI self-test (no HID)\n'
QT_QPA_PLATFORM=offscreen ./.tmp/kencoder-notify-gui-build/kencoder --self-test
printf '==> Stage optional KCM locally (no sudo)\n'
DESTDIR="$PWD/.tmp/kencoder-notify-stage" cmake --install .tmp/kencoder-notify-gui-build --prefix /usr

work=$(mktemp -d "$PWD/.tmp/kencoder-notify-nohid.XXXXXX")
trap 'rm -rf "$work"' EXIT
export XDG_CONFIG_HOME="$work/config"
mkdir -p "$XDG_CONFIG_HOME/kencoder" "$work/mockbin"

printf '==> Default configuration must be disabled\n'
"$bin" --check | tee "$work/default.log"
grep -Fq 'enabled=0' "$work/default.log"

cat > "$XDG_CONFIG_HOME/kencoder/notify-presets.json" <<'JSON'
{
  "version": 1,
  "presets": [
    {"id":"test-waves", "name":"Dvě vlny", "steps":[
      {"type":"left_wave", "brightness":1, "speed":2, "duration_ms":50},
      {"type":"right_wave", "brightness":1, "speed":2, "duration_ms":50}
    ]},
    {"id":"test-blink", "name":"Dvě bliknutí", "steps":[
      {"type":"off", "duration_ms":50},
      {"type":"static", "brightness":1, "duration_ms":50},
      {"type":"off", "duration_ms":50},
      {"type":"static", "brightness":1, "duration_ms":50}
    ]}
  ]
}
JSON
cat > "$XDG_CONFIG_HOME/kencoder/notify-rules.json" <<'JSON'
{
  "version": 1,
  "enabled": false,
  "backend": "kde-watcher",
  "rules": [{"id":"teams-xyz", "name":"Teams XYZ", "enabled":true,
    "match":{"app_contains":"Teams", "title_contains":"", "text_contains":"XYZ"},
    "preset_id":"test-blink", "cooldown_ms":5000}]
}
JSON
printf '==> Validate sample JSON\n'
"$bin" --check | tee "$work/valid.log"
grep -Fq 'presets=3; rules=1' "$work/valid.log"

printf '==> Match Teams notification without DBus or HID\n'
"$bin" --simulate 'Microsoft Teams' 'Chat' 'Obsah XYZ' | tee "$work/matches.log"
grep -Fxq 'MATCH teams-xyz' "$work/matches.log"
"$bin" --simulate 'Browser' 'Chat' 'Obsah XYZ' | tee "$work/nonmatches.log"
grep -Fxq 'MATCHES 0' "$work/nonmatches.log"

printf '==> Verify dry-run never executes CLI\n'
"$bin" --play test-waves --dry-run | tee "$work/dry-run.log"
grep -Fq 'encoder -b 1 -a left_wave -v medium; hold 50ms' "$work/dry-run.log"
grep -Fq 'encoder -b 1 -a right_wave -v medium; hold 50ms' "$work/dry-run.log"
grep -Fq 'restore-default: encoder -b 1 -s; no hold' "$work/dry-run.log"

printf '==> Verify actual sequencer with MOCK encoder only\n'
cat > "$work/mockbin/encoder" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$KENCODER_NOTIFY_TEST_CALLS"
# Legacy encoder can print this warning while exiting with status 0.
if [[ ${KENCODER_NOTIFY_TEST_FAIL_ARGUMENTS:-} == "$*" ]]; then
    printf 'Command failed\n'
fi
if [[ ${KENCODER_NOTIFY_TEST_EXIT_FAILURE:-} == "$*" ]]; then
    exit 7
fi
MOCK
chmod 755 "$work/mockbin/encoder"
export KENCODER_NOTIFY_TEST_CALLS="$work/mock-calls.log"
# Explicit non-existent session-bus address prevents forwarding to a live daemon.
DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-dbus-server" \
  PATH="$work/mockbin:$PATH" "$bin" --play test-blink
mapfile -t calls < "$KENCODER_NOTIFY_TEST_CALLS"
[[ ${#calls[@]} -eq 5 ]]
[[ ${calls[0]} == '-o' && ${calls[1]} == '-b 1 -s' &&
   ${calls[2]} == '-o' && ${calls[3]} == '-b 1 -s' &&
   ${calls[4]} == '-b 1 -s' ]]
printf '==> Default applies one state, with no duration or recursive restore\n'
: > "$KENCODER_NOTIFY_TEST_CALLS"
DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-dbus-server" \
  PATH="$work/mockbin:$PATH" "$bin" --play default
mapfile -t default_calls < "$KENCODER_NOTIFY_TEST_CALLS"
[[ ${#default_calls[@]} -eq 1 && ${default_calls[0]} == '-b 1 -s' ]]
printf 'PASS: mock sequence; no real HID I/O\n'

printf '==> Manual playback restores the latest saved Default\n'
# The user can edit Default while the KCM remains open. Check the on-disk value,
# and require it to be the last MOCK encoder command after a manual --play.
sed -i '/"presets": \[/a\    {"id":"default","name":"Default","steps":[{"type":"static","brightness":3,"duration_ms":50}]},' \
  "$XDG_CONFIG_HOME/kencoder/notify-presets.json"
: > "$KENCODER_NOTIFY_TEST_CALLS"
DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-dbus-server" \
  PATH="$work/mockbin:$PATH" "$bin" --play test-waves
mapfile -t manual_calls < "$KENCODER_NOTIFY_TEST_CALLS"
[[ ${#manual_calls[@]} -eq 3 && ${manual_calls[0]} == '-b 1 -a left_wave -v medium' &&
   ${manual_calls[1]} == '-b 1 -a right_wave -v medium' &&
   ${manual_calls[2]} == '-b 3 -s' ]]
printf 'PASS: manual playback restored saved Default via MOCK only\n'

printf '==> Effect warning must stop remaining effects and restore Default\n'
: > "$KENCODER_NOTIFY_TEST_CALLS"
if KENCODER_NOTIFY_TEST_FAIL_ARGUMENTS='-b 1 -a left_wave -v medium' \
   DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-dbus-server" \
   PATH="$work/mockbin:$PATH" "$bin" --play test-waves; then
    printf 'FAIL: failed effect was reported as success\n' >&2
    exit 1
fi
mapfile -t failure_calls < "$KENCODER_NOTIFY_TEST_CALLS"
[[ ${#failure_calls[@]} -eq 2 &&
   ${failure_calls[0]} == '-b 1 -a left_wave -v medium' &&
   ${failure_calls[1]} == '-b 3 -s' ]]

printf '==> Failed final effect still restores Default\n'
: > "$KENCODER_NOTIFY_TEST_CALLS"
if KENCODER_NOTIFY_TEST_EXIT_FAILURE='-b 1 -a right_wave -v medium' \
   DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-dbus-server" \
   PATH="$work/mockbin:$PATH" "$bin" --play test-waves; then
    printf 'FAIL: failed last effect was reported as success\n' >&2
    exit 1
fi
mapfile -t failure_calls < "$KENCODER_NOTIFY_TEST_CALLS"
[[ ${#failure_calls[@]} -eq 3 &&
   ${failure_calls[2]} == '-b 3 -s' ]]

printf '==> Failed Default restoration must be reported, never retried\n'
: > "$KENCODER_NOTIFY_TEST_CALLS"
if KENCODER_NOTIFY_TEST_FAIL_ARGUMENTS='-b 3 -s' \
   DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-dbus-server" \
   PATH="$work/mockbin:$PATH" "$bin" --play test-waves; then
    printf 'FAIL: failed Default restoration was reported as success\n' >&2
    exit 1
fi
mapfile -t failure_calls < "$KENCODER_NOTIFY_TEST_CALLS"
[[ ${#failure_calls[@]} -eq 3 &&
   ${failure_calls[2]} == '-b 3 -s' ]]
printf 'PASS: restoration attempted on failures, bounded to one attempt\n'

printf '==> Isolated D-Bus daemon also restores Default after a failed effect\n'
if command -v dbus-run-session >/dev/null 2>&1 && command -v dbus-send >/dev/null 2>&1; then
    dbus-run-session -- bash -c '
        set -euo pipefail
        bin=$1
        work=$2
        export XDG_CONFIG_HOME="$work/config"
        export PATH="$work/mockbin:$PATH"
        export KENCODER_NOTIFY_TEST_CALLS="$work/dbus-calls.log"
        export KENCODER_NOTIFY_TEST_FAIL_ARGUMENTS="-b 1 -a left_wave -v medium"
        : > "$KENCODER_NOTIFY_TEST_CALLS"
        "$bin" --daemon > "$work/private-daemon.log" 2>&1 &
        daemon_pid=$!
        trap "kill $daemon_pid 2>/dev/null || true; wait $daemon_pid 2>/dev/null || true" EXIT
        ready=0
        for ((try=0; try<50; try++)); do
            if dbus-send --session --print-reply --dest=org.kencoder.Notify1 \
                /org/kencoder/Notify1 org.kencoder.Notify1.PlaybackProtocolVersion \
                > "$work/private-protocol.log" 2>&1 &&
                grep -Fq "uint32 2" "$work/private-protocol.log"; then
                ready=1
                break
            fi
            sleep 0.1
        done
        [[ $ready == 1 ]] || { printf "FAIL: private D-Bus daemon not ready\n" >&2; exit 1; }
        "$bin" --play test-waves
        for ((try=0; try<60; try++)); do
            mapfile -t bus_calls < "$KENCODER_NOTIFY_TEST_CALLS"
            [[ ${#bus_calls[@]} -ge 2 ]] && break
            sleep 0.1
        done
        [[ ${#bus_calls[@]} -eq 2 &&
           ${bus_calls[0]} == "-b 1 -a left_wave -v medium" &&
           ${bus_calls[1]} == "-b 3 -s" ]] || {
            printf "FAIL: private daemon failed Default restore\n" >&2
            cat "$work/private-daemon.log" >&2
            exit 1
        }
    ' bash "$bin" "$work"
    printf 'PASS: private D-Bus daemon restored Default with mock CLI\n'
else
    printf 'SKIP: dbus-run-session or dbus-send not installed\n'
fi

printf '==> Disabled rule must not match\n'
sed -i 's/"id":"teams-xyz", "name":"Teams XYZ", "enabled":true/"id":"teams-xyz", "name":"Teams XYZ", "enabled":false/' \
  "$XDG_CONFIG_HOME/kencoder/notify-rules.json"
"$bin" --simulate 'Microsoft Teams' 'Chat' 'Obsah XYZ' | tee "$work/disabled.log"
grep -Fxq 'MATCHES 0' "$work/disabled.log"

printf '==> Rule priority: first matching enabled rule wins\n'
cat > "$XDG_CONFIG_HOME/kencoder/notify-rules.json" <<'JSON'
{
  "version": 1, "enabled": false, "backend": "kde-watcher",
  "rules": [
    {"id":"specific", "name":"Specific", "enabled":true,
      "match":{"app_contains":"Teams", "title_contains":"", "text_contains":"XYZ"},
      "preset_id":"test-waves", "cooldown_ms":0},
    {"id":"generic", "name":"Generic", "enabled":true,
      "match":{"app_contains":"Teams", "title_contains":"", "text_contains":""},
      "preset_id":"test-blink", "cooldown_ms":0}
  ]
}
JSON
"$bin" --simulate 'Microsoft Teams' 'Chat' 'Obsah XYZ' | tee "$work/priority-specific.log"
grep -Fxq 'MATCH specific' "$work/priority-specific.log"
grep -Fxq 'MATCHES 1' "$work/priority-specific.log"
if grep -Fq 'MATCH generic' "$work/priority-specific.log"; then
    printf 'FAIL: generic rule was triggered after specific rule\n' >&2
    exit 1
fi
# Reverse the JSON array order to verify effective rule priority.
cat > "$XDG_CONFIG_HOME/kencoder/notify-rules.json" <<'JSON'
{
  "version": 1, "enabled": false, "backend": "kde-watcher",
  "rules": [
    {"id":"generic", "name":"Generic", "enabled":true,
      "match":{"app_contains":"Teams", "title_contains":"", "text_contains":""},
      "preset_id":"test-blink", "cooldown_ms":0},
    {"id":"specific", "name":"Specific", "enabled":true,
      "match":{"app_contains":"Teams", "title_contains":"", "text_contains":"XYZ"},
      "preset_id":"test-waves", "cooldown_ms":0}
  ]
}
JSON
"$bin" --simulate 'Microsoft Teams' 'Chat' 'Obsah XYZ' | tee "$work/priority-generic.log"
grep -Fxq 'MATCH generic' "$work/priority-generic.log"
grep -Fxq 'MATCHES 1' "$work/priority-generic.log"
if grep -Fq 'MATCH specific' "$work/priority-generic.log"; then
    printf 'FAIL: specific rule was triggered after generic rule\n' >&2
    exit 1
fi

printf '==> Invalid backend must fail closed\n'
sed -i 's/"enabled": false/"enabled": true/;s/"kde-watcher"/"knotify-scanner"/' \
  "$XDG_CONFIG_HOME/kencoder/notify-rules.json"
if "$bin" --check > "$work/rejected.log" 2>&1; then
  echo 'FAIL: unsupported backend was accepted' >&2
  exit 1
fi
printf 'PASS: malformed or unsupported config rejected\n'
printf 'PASS: notify phase-1 build and tests\nKCM stage: %s\nLog: %s\n' "$PWD/.tmp/kencoder-notify-stage" "$log"

#!/usr/bin/env bash
# Build KEncoder Notify and exercise config / playback with an isolated mock.
# NEVER invokes the real encoder; never installs a package or starts a listener.
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
grep -Fq 'presets=2; rules=1' "$work/valid.log"

printf '==> Match Teams notification without DBus or HID\n'
"$bin" --simulate 'Microsoft Teams' 'Chat' 'Obsah XYZ' | tee "$work/matches.log"
grep -Fxq 'MATCH teams-xyz' "$work/matches.log"
"$bin" --simulate 'Browser' 'Chat' 'Obsah XYZ' | tee "$work/nonmatches.log"
grep -Fxq 'MATCHES 0' "$work/nonmatches.log"

printf '==> Verify dry-run never executes CLI\n'
"$bin" --play test-waves --dry-run | tee "$work/dry-run.log"
grep -Fq 'encoder -b 1 -a left_wave -v medium; hold 50ms' "$work/dry-run.log"
grep -Fq 'encoder -b 1 -a right_wave -v medium; hold 50ms' "$work/dry-run.log"

printf '==> Verify actual sequencer with MOCK encoder only\n'
cat > "$work/mockbin/encoder" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$KENCODER_NOTIFY_TEST_CALLS"
MOCK
chmod 755 "$work/mockbin/encoder"
export KENCODER_NOTIFY_TEST_CALLS="$work/mock-calls.log"
# Explicit non-existent session-bus address prevents forwarding to a live daemon.
DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-dbus-server" \
  PATH="$work/mockbin:$PATH" "$bin" --play test-blink
mapfile -t calls < "$KENCODER_NOTIFY_TEST_CALLS"
[[ ${#calls[@]} -eq 4 ]]
[[ ${calls[0]} == '-o' && ${calls[1]} == '-b 1 -s' &&
   ${calls[2]} == '-o' && ${calls[3]} == '-b 1 -s' ]]
printf 'PASS: mock sequence; no real HID I/O\n'

printf '==> Invalid backend must fail closed\n'
sed -i 's/"enabled": false/"enabled": true/;s/"kde-watcher"/"knotify-scanner"/' \
  "$XDG_CONFIG_HOME/kencoder/notify-rules.json"
if "$bin" --check > "$work/rejected.log" 2>&1; then
  echo 'FAIL: unsupported backend was accepted' >&2
  exit 1
fi
printf 'PASS: malformed or unsupported config rejected\n'
printf 'PASS: notify phase-1 build and tests\nKCM stage: %s\nLog: %s\n' "$PWD/.tmp/kencoder-notify-stage" "$log"

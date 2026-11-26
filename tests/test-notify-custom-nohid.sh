#!/usr/bin/env bash
# Custom backend regression, independent private D-Bus + mock encoder.
# Does not inspect the real desktop session or write to /dev/hidraw.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
for cmd in cmake dbus-run-session dbus-send; do
    command -v "$cmd" >/dev/null || { echo "Missing prerequisite: $cmd" >&2; exit 2; }
done
mkdir -p .tmp .log
log=.log/kencoder-notify-custom-nohid.log
exec > >(tee "$log") 2>&1
cmake -S notify -B .tmp/kencoder-notify-build -DCMAKE_BUILD_TYPE=Debug
cmake --build .tmp/kencoder-notify-build --parallel 2
bin=$PWD/.tmp/kencoder-notify-build/kencoder-notify
work=$(mktemp -d "$PWD/.tmp/kencoder-custom-nohid.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/config/kencoder" "$work/mockbin"
cat > "$work/config/kencoder/notify-presets.json" <<'JSON'
{"version":1,"presets":[
  {"id":"default","name":"Default","steps":[{"type":"static","brightness":1,"duration_ms":50}]},
  {"id":"pulse","name":"Pulse","steps":[{"type":"static","brightness":3,"duration_ms":50}]}
]}
JSON
cat > "$work/config/kencoder/notify-rules.json" <<'JSON'
{"version":1,"enabled":true,"backend":"custom-api","rules":[
  {"id":"custom-rule","name":"Custom","enabled":true,
   "match":{"app_contains":"my-bridge","title_contains":"build","text_contains":""},
   "preset_id":"pulse","cooldown_ms":5000}
]}
JSON
cat > "$work/mockbin/encoder" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$KENCODER_NOTIFY_TEST_CALLS"
MOCK
chmod 755 "$work/mockbin/encoder"
export KENCODER_NOTIFY_TEST_CALLS="$work/calls.log"
: > "$KENCODER_NOTIFY_TEST_CALLS"
XDG_CONFIG_HOME="$work/config" "$bin" --check | grep -q 'backend=custom-api'

dbus-run-session -- bash -c '
    set -euo pipefail
    bin=$1; work=$2
    export XDG_CONFIG_HOME="$work/config"
    export KENCODER_NOTIFY_TEST_CALLS="$work/calls.log"
    export PATH="$work/mockbin:$PATH"
    "$bin" --daemon > "$work/daemon.log" 2>&1 &
    pid=$!
    trap "kill $pid 2>/dev/null || true; wait $pid 2>/dev/null || true" EXIT

    call() {
        dbus-send --session --print-reply --dest=org.kencoder.Notify1 \
            /org/kencoder/Notify1 "org.kencoder.Notify1.$1"
    }
    reload_config() { call Reload > "$work/reload.out"; grep -Fq "boolean true" "$work/reload.out"; }
    wait_calls() {
        local want=$1 i count
        for ((i=0;i<80;i++)); do
            count=$(wc -l < "$KENCODER_NOTIFY_TEST_CALLS")
            [[ $count -ge $want ]] && return 0
            sleep 0.05
        done
        echo "Expected $want mock calls, found $(wc -l < "$KENCODER_NOTIFY_TEST_CALLS")" >&2
        cat "$work/daemon.log" >&2
        return 1
    }
    ready=0
    for ((i=0;i<50;i++)); do
        if call SubmissionProtocolVersion > "$work/protocol.out" 2>&1 &&
            grep -Fq "uint32 1" "$work/protocol.out"; then ready=1; break; fi
        sleep 0.1
    done
    [[ $ready -eq 1 ]] || { cat "$work/daemon.log" >&2; exit 1; }
    call ListenerStatus > "$work/status.out"
    grep -Fq "string \"active\"" "$work/status.out"
    "$bin" --submit my-bridge "Build finished" "CI succeeded"
    wait_calls 2
    mapfile -t cmds < "$KENCODER_NOTIFY_TEST_CALLS"
    [[ ${#cmds[@]} -eq 2 && ${cmds[0]} == "-b 3 -s" && ${cmds[1]} == "-b 1 -s" ]]
    printf "PASS: custom event -> matching rule -> pulse -> Default\n"

    if "$bin" --submit my-bridge "Build finished" "CI succeeded"; then
        echo "FAIL: cooldown bypass" >&2; exit 1
    else [[ $? -eq 4 ]]; fi
    if "$bin" --submit other-bridge "Build finished" "CI succeeded"; then
        echo "FAIL: unmatched event accepted" >&2; exit 1
    else [[ $? -eq 4 ]]; fi
    if "$bin" --submit "" "" ""; then
        echo "FAIL: empty event accepted" >&2; exit 1
    else [[ $? -eq 4 ]]; fi
    [[ $(wc -l < "$KENCODER_NOTIFY_TEST_CALLS") -eq 2 ]]
    printf "PASS: cooldown, unmatched and empty events rejected\n"

    # Switch to the native backend, where submission must be rejected. KDE is
    # intentionally absent on this private bus; this must not block reload.
    sed -i "s/custom-api/kde-watcher/" "$XDG_CONFIG_HOME/kencoder/notify-rules.json"
    reload_config
    if "$bin" --submit my-bridge "Build finished" "CI succeeded"; then
        echo "FAIL: custom API accepted while KDE watcher selected" >&2; exit 1
    else [[ $? -eq 4 ]]; fi
    [[ $(wc -l < "$KENCODER_NOTIFY_TEST_CALLS") -eq 2 ]]
    printf "PASS: active backend selection gates custom events\n"

    # Disable listening while custom backend is selected.
    sed -i "s/kde-watcher/custom-api/;s/\"enabled\":true,\"backend\"/\"enabled\":false,\"backend\"/" \
        "$XDG_CONFIG_HOME/kencoder/notify-rules.json"
    reload_config
    if "$bin" --submit my-bridge "Build finished" "CI succeeded"; then
        echo "FAIL: disabled backend accepted" >&2; exit 1
    else [[ $? -eq 4 ]]; fi
    call ListenerStatus > "$work/status-disabled.out"
    grep -Fq "string \"disabled\"" "$work/status-disabled.out"
    printf "PASS: disabled backend rejects submissions\n"

    sed -i "s/\"enabled\":false,\"backend\"/\"enabled\":true,\"backend\"/" \
        "$XDG_CONFIG_HOME/kencoder/notify-rules.json"
    reload_config
    # Direct API invocation, without using the CLI convenience wrapper.
    dbus-send --session --print-reply --dest=org.kencoder.Notify1 \
        /org/kencoder/Notify1 org.kencoder.Notify1.SubmitNotification \
        string:my-bridge string:"Build finished" string:"CI succeeded" > "$work/direct.out"
    grep -Fq "boolean true" "$work/direct.out"
    wait_calls 4
    mapfile -t cmds < "$KENCODER_NOTIFY_TEST_CALLS"
    [[ ${#cmds[@]} -eq 4 && ${cmds[2]} == "-b 3 -s" && ${cmds[3]} == "-b 1 -s" ]]
    printf "PASS: public D-Bus API, re-enabling and Default restoration\n"
' bash "$bin" "$work"

printf 'PASS: custom backend no-HID regression; log: %s\n' "$log"

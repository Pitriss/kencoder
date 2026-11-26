#!/usr/bin/env bash
# Isolated mid-playback backend switch regression. No real HID or system installation.
set -euo pipefail
# Run this script from the kencoder project root (or with KENCODER_ROOT set).
root=${KENCODER_ROOT:-$PWD}
cd "$root"
for cmd in cmake dbus-run-session dbus-send python3; do
    command -v "$cmd" >/dev/null || { printf 'Missing prerequisite: %s\n' "$cmd" >&2; exit 2; }
done
[[ -f notify/CMakeLists.txt && -f notify/main.cpp ]] || { echo 'Run in kencoder root or set KENCODER_ROOT' >&2; exit 2; }
mkdir -p .tmp .log
log=.log/kencoder-notify-backend-switch-nohid-v3.log
work=$(mktemp -d "$PWD/.tmp/kencoder-backend-switch.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/config/kencoder" "$work/mockbin"
cat > "$work/config/kencoder/notify-presets.json" <<'JSON'
{"version":1,"presets":[
  {"id":"default","name":"Default","steps":[{"type":"static","brightness":1,"duration_ms":50}]},
  {"id":"pulse","name":"Pulse","steps":[{"type":"static","brightness":3,"duration_ms":1800}]}
]}
JSON
cat > "$work/config/kencoder/notify-rules.json" <<'JSON'
{"version":1,"enabled":true,"backend":"custom-api","rules":[
  {"id":"custom-rule","name":"Custom","enabled":true,
   "match":{"app_contains":"my-bridge","title_contains":"build","text_contains":""},
   "preset_id":"pulse","cooldown_ms":0}
]}
JSON
cat > "$work/mockbin/encoder" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$KENCODER_NOTIFY_TEST_CALLS"
MOCK
chmod 755 "$work/mockbin/encoder"
: > "$work/calls.log"
if ! cmake -S notify -B .tmp/kencoder-notify-build -DCMAKE_BUILD_TYPE=Debug > "$log" 2>&1 ||
   ! cmake --build .tmp/kencoder-notify-build --parallel 2 >> "$log" 2>&1; then
    tail -n 25 "$log" >&2; exit 1
fi
bin="$PWD/.tmp/kencoder-notify-build/kencoder-notify"
if ! XDG_CONFIG_HOME="$work/config" "$bin" --check >> "$log" 2>&1; then
    tail -n 20 "$log" >&2; exit 1
fi
if dbus-run-session -- bash -c '
    set -euo pipefail
    bin=$1; work=$2
    export XDG_CONFIG_HOME="$work/config"
    export KENCODER_NOTIFY_TEST_CALLS="$work/calls.log"
    export PATH="$work/mockbin:$PATH"
    "$bin" --daemon > "$work/daemon.log" 2>&1 &
    pid=$!
    trap '\''kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true'\'' EXIT
    call() {
        dbus-send --session --print-reply --reply-timeout=60000 --dest=org.kencoder.Notify1 \
            /org/kencoder/Notify1 "org.kencoder.Notify1.$1"
    }
    wait_count() {
        local target=$1 i n
        for ((i=0;i<140;i++)); do
            n=$(wc -l < "$KENCODER_NOTIFY_TEST_CALLS")
            if (( n >= target )); then return 0; fi
            sleep 0.05
        done
        echo "FAIL: expected $target mock encoder calls, got $n" >&2
        cat "$work/daemon.log" >&2
        return 1
    }
    ready=0
    for ((i=0;i<60;i++)); do
        if call SubmissionProtocolVersion > "$work/version.out" 2>&1 &&
            grep -Fq "uint32 1" "$work/version.out"; then ready=1; break; fi
        sleep 0.05
    done
    [[ $ready -eq 1 ]] || { cat "$work/daemon.log" >&2; exit 1; }
    # The daemon may temporarily block in KDE Notifications auto-activation.
    # Use one direct DBus request with an explicit, sufficiently long timeout.
    # The CLI --submit has a 1500-ms timeout and is tested separately.
    if ! dbus-send --session --print-reply --reply-timeout=60000 \
        --dest=org.kencoder.Notify1 /org/kencoder/Notify1 \
        org.kencoder.Notify1.SubmitNotification \
        string:my-bridge string:"Build finished" string:"CI succeeded" \
        > "$work/submit.out" 2> "$work/submit.err"; then
        echo "FAIL: first submission did not receive a DBus reply" >&2
        cat "$work/submit.err" "$work/daemon.log" >&2
        exit 1
    fi
    grep -Fq "boolean true" "$work/submit.out" || {
        echo "FAIL: first submission was rejected" >&2
        cat "$work/submit.out" "$work/daemon.log" >&2
        exit 1
    }
    wait_count 1
    [[ $(wc -l < "$KENCODER_NOTIFY_TEST_CALLS") -eq 1 ]] || { echo "FAIL: sequence was not active" >&2; exit 1; }
    python3 - "$XDG_CONFIG_HOME/kencoder/notify-rules.json" <<'\''PY'\''
import json, pathlib, sys
p = pathlib.Path(sys.argv[1]); obj=json.loads(p.read_text()); obj["backend"]="kde-watcher"
p.write_text(json.dumps(obj)+"\n")
PY
    call Reload > "$work/reload.out"
    grep -Fq "boolean true" "$work/reload.out" || { echo "FAIL: reload not acknowledged" >&2; exit 1; }
    if ! dbus-send --session --print-reply --reply-timeout=60000 \
        --dest=org.kencoder.Notify1 /org/kencoder/Notify1 \
        org.kencoder.Notify1.SubmitNotification \
        string:my-bridge string:"Build finished" string:"CI succeeded" \
        > "$work/rejected.out" 2> "$work/rejected.err"; then
        echo "FAIL: DBus rejected-submission call failed" >&2
        cat "$work/rejected.err" "$work/daemon.log" >&2
        exit 1
    fi
    grep -Fq "boolean false" "$work/rejected.out" || {
        echo "FAIL: custom submission accepted after backend switch" >&2
        cat "$work/rejected.out" >&2
        exit 1
    }
    wait_count 2
    # A running effect must finish and restore Default; no third effect.
    sleep 0.25
    mapfile -t cmds < "$KENCODER_NOTIFY_TEST_CALLS"
    [[ ${#cmds[@]} -eq 2 && ${cmds[0]} == "-b 3 -s" && ${cmds[1]} == "-b 1 -s" ]] || {
        printf "FAIL: unexpected encoder sequence: %s\n" "${cmds[*]}" >&2; exit 1;
    }
    printf "PASS: backend changed mid-playback; old input rejected; Default restored\n"
' bash "$bin" "$work" >> "$log" 2>&1; then
    grep -E 'PASS:|FAIL:' "$log" | tail -n 10
else
    tail -n 30 "$log" >&2
    exit 1
fi
printf 'PASS: isolated backend switch no-HID test; log: %s\n' "$log"

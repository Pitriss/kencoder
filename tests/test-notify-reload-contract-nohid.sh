#!/usr/bin/env bash
# Test the D-Bus Reload contract used by KCM in an isolated private session.
# Does not launch the actual KCM UI, install anything or access real HID devices.
set -euo pipefail
root=${KENCODER_ROOT:-$PWD}
cd "$root"
for cmd in cmake dbus-run-session dbus-send python3; do
    command -v "$cmd" >/dev/null || { printf 'Missing command: %s\n' "$cmd" >&2; exit 2; }
done
[[ -f notify/main.cpp && -f notify/CMakeLists.txt ]] || {
    echo 'Run from kencoder repository root (or set KENCODER_ROOT)' >&2; exit 2;
}
mkdir -p .log .tmp
log=.log/kencoder-notify-reload-contract-nohid-v1.log
work=$(mktemp -d "$PWD/.tmp/kencoder-reload-contract.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/config/kencoder" "$work/mockbin"
cat > "$work/config/kencoder/notify-presets.json" <<'JSON'
{"version":1,"presets":[
  {"id":"default","name":"Default","steps":[{"type":"static","brightness":1,"duration_ms":50}]},
  {"id":"pulse","name":"Pulse","steps":[{"type":"static","brightness":3,"duration_ms":50}]}
]}
JSON
cat > "$work/config/kencoder/notify-rules.json" <<'JSON'
{"version":1,"enabled":true,"backend":"custom-api","rules":[
  {"id":"reload-rule","name":"Reload","enabled":true,
   "match":{"app_contains":"reload-test","title_contains":"complete","text_contains":""},
   "preset_id":"pulse","cooldown_ms":0}
]}
JSON
cat > "$work/mockbin/encoder" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$KENCODER_NOTIFY_TEST_CALLS"
MOCK
# Keep session/DPMS queries inside the private test environment.
cat > "$work/mockbin/kscreen-doctor" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' 'dpms mode for screen virtual-1: on'
MOCK
cat > "$work/mockbin/xset" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' 'Monitor is On'
MOCK
chmod 755 "$work/mockbin/encoder" "$work/mockbin/kscreen-doctor" "$work/mockbin/xset"
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
        local method=$1
        shift
        dbus-send --session --print-reply --reply-timeout=60000 \
            --dest=org.kencoder.Notify1 /org/kencoder/Notify1 "org.kencoder.Notify1.$method" "$@"
    }
    assert_bool() {
        local operation=$1 expected=$2
        shift 2
        if ! call "$operation" "$@" > "$work/reply.out" 2> "$work/reply.err"; then
            printf "FAIL: no D-Bus reply for %s\n" "$operation" >&2
            cat "$work/reply.err" "$work/daemon.log" >&2
            exit 1
        fi
        if ! grep -Fq "boolean $expected" "$work/reply.out"; then
            printf "FAIL: %s expected boolean %s\n" "$operation" "$expected" >&2
            cat "$work/reply.out" "$work/daemon.log" >&2
            exit 1
        fi
    }
    wait_calls() {
        local wanted=$1 i count
        for ((i=0; i<140; i++)); do
            count=$(wc -l < "$KENCODER_NOTIFY_TEST_CALLS")
            if (( count >= wanted )); then return; fi
            sleep 0.05
        done
        printf "FAIL: mock encoder calls expected >= %s\n" "$wanted" >&2
        cat "$work/daemon.log" >&2
        exit 1
    }
    change_config() {
        local enabled=$1 backend=$2
        python3 - "$XDG_CONFIG_HOME/kencoder/notify-rules.json" "$enabled" "$backend" <<'\''PY'\''
import json, pathlib, sys
p=pathlib.Path(sys.argv[1]); obj=json.loads(p.read_text())
obj["enabled"] = sys.argv[2] == "true"
obj["backend"] = sys.argv[3]
p.write_text(json.dumps(obj) + "\n")
PY
    }
    ready=0
    for ((i=0;i<80;i++)); do
        if call SubmissionProtocolVersion > "$work/proto.out" 2>&1 &&
           grep -Fq "uint32 1" "$work/proto.out"; then ready=1; break; fi
        sleep 0.05
    done
    [[ $ready -eq 1 ]] || { echo "FAIL: daemon did not register" >&2; cat "$work/daemon.log" >&2; exit 1; }
    # KCM saves the file first, then calls Reload(): test that exact order.
    change_config false custom-api
    assert_bool Reload true
    call ListenerStatus > "$work/status.out"
    grep -Fq '\''string "disabled"'\'' "$work/status.out" || { echo "FAIL: disabled status" >&2; exit 1; }
    assert_bool SubmitNotification false string:reload-test string:complete string:done
    # Re-enable using the other supported input without submitting a KDE event.
    change_config true kde-watcher
    assert_bool Reload true
    assert_bool SubmitNotification false string:reload-test string:complete string:done
    # Re-enable custom-api and test the API before waiting for filesystem watcher.
    change_config true custom-api
    assert_bool Reload true
    call ListenerStatus > "$work/status.out"
    grep -Fq '\''string "active"'\'' "$work/status.out" || { echo "FAIL: custom listener not active" >&2; exit 1; }
    assert_bool SubmitNotification true string:reload-test string:complete string:done
    wait_calls 2
    # Malformed backend must be rejected by Reload() and fail closed.
    change_config true invalid-backend
    assert_bool Reload false
    assert_bool SubmitNotification false string:reload-test string:complete string:done
    change_config true custom-api
    assert_bool Reload true
    assert_bool SubmitNotification true string:reload-test string:complete string:done
    wait_calls 4
    mapfile -t cmds < "$KENCODER_NOTIFY_TEST_CALLS"
    [[ ${#cmds[@]} -eq 4 && ${cmds[0]} == "-b 3 -s" && ${cmds[1]} == "-b 1 -s" &&
       ${cmds[2]} == "-b 3 -s" && ${cmds[3]} == "-b 1 -s" ]] || {
        printf "FAIL: unexpected mock lighting: %s\n" "${cmds[*]}" >&2
        exit 1
    }
    printf "PASS: KCM save + Reload contract, disabled and switched backends\n"
    printf "PASS: malformed Reload rejected; restored API and Default (no HID)\n"
' bash "$bin" "$work" >> "$log" 2>&1; then
    grep -E 'PASS:|FAIL:' "$log" | tail -n 10
else
    tail -n 30 "$log" >&2
    exit 1
fi
printf 'PASS: isolated D-Bus Reload contract test; log: %s\n' "$log"

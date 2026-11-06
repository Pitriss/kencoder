#!/usr/bin/env bash
# Dedicated session policy regression test, only MOCK encoder, no hardware writes.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
log=.log/kencoder-session-policy-nohid-test.log
exec > >(tee "$log") 2>&1
bin="$PWD/.tmp/kencoder-notify-build/kencoder-notify"
[[ -x "$bin" ]] || { echo 'Build listener with tests/test-notify-nohid.sh first' >&2; exit 1; }
work=$(mktemp -d "$PWD/.tmp/kencoder-session-nohid.XXXXXX")
trap 'rm -rf "$work"' EXIT
export XDG_CONFIG_HOME="$work/config"
mkdir -p "$XDG_CONFIG_HOME/kencoder" "$work/mockbin"
cat > "$XDG_CONFIG_HOME/kencoder/notify-presets.json" <<'JSON'
{"version":1,"presets":[{"id":"default","name":"Default", "steps":[{"type":"static","brightness":2,"duration_ms":50}]}]}
JSON
cat > "$XDG_CONFIG_HOME/kencoder/notify-rules.json" <<'JSON'
{"version":1,"enabled":false,"backend":"kde-watcher","rules":[]}
JSON
cat > "$XDG_CONFIG_HOME/kencoder/session-policy.json" <<'JSON'
{"version":1,"off_on_lock":false,"off_on_screen_off":true,"notify_when_inactive":false}
JSON
"$bin" --session-policy | tee "$work/policy.log"
grep -Fq 'lock=0; screen-off=1; notify-when-inactive=0' "$work/policy.log"
cat > "$work/mockbin/encoder" <<'MOCK'
#!/bin/sh
printf '%s\n' "$*" >> "$KENCODER_SESSION_TEST_CALLS"
MOCK
cat > "$work/mockbin/kscreen-doctor" <<'MOCK'
#!/bin/sh
printf 'dpms mode for screen DP-1: %s\n' "$(cat "$KENCODER_SESSION_TEST_DPMS")"
MOCK
chmod 755 "$work/mockbin/encoder" "$work/mockbin/kscreen-doctor"
printf 'off\n' > "$work/dpms-mode"
printf '==> Private D-Bus + mock DPMS: off, then wake to Default\n'
if command -v dbus-run-session >/dev/null 2>&1 && command -v dbus-send >/dev/null 2>&1; then
    dbus-run-session -- bash -c '
        set -euo pipefail
        bin=$1
        work=$2
        export PATH="$work/mockbin:$PATH" XDG_CONFIG_HOME="$work/config"
        export XDG_SESSION_TYPE=wayland KENCODER_SESSION_TEST_DPMS="$work/dpms-mode"
        export KENCODER_SESSION_TEST_CALLS="$work/encoder-calls.log"
        : > "$KENCODER_SESSION_TEST_CALLS"
        "$bin" --daemon > "$work/private-session.log" 2>&1 &
        daemon=$!
        trap "kill $daemon 2>/dev/null || true; wait $daemon 2>/dev/null || true" EXIT
        ready=0
        for ((i=0; i<40; i++)); do
            if dbus-send --session --print-reply --dest=org.kencoder.Notify1                 /org/kencoder/Notify1 org.kencoder.Notify1.SessionPolicyProtocolVersion                 > "$work/protocol.log" 2>&1 && grep -Fq "uint32 1" "$work/protocol.log"; then
                ready=1
                break
            fi
            sleep 0.1
        done
        [[ $ready == 1 ]] || { cat "$work/private-session.log" >&2; echo "FAIL: session daemon unavailable" >&2; exit 1; }
        offSeen=0
        for ((i=0; i<50; i++)); do
            if grep -Fxq -- "-o" "$KENCODER_SESSION_TEST_CALLS"; then offSeen=1; break; fi
            sleep 0.1
        done
        if [[ $offSeen != 1 ]]; then
            cat "$work/private-session.log" >&2
            echo "FAIL: did not turn off after DPMS-off" >&2
            exit 1
        fi
        printf "on\n" > "$KENCODER_SESSION_TEST_DPMS"
        restored=0
        for ((i=0; i<85; i++)); do
            if grep -Fxq -- "-b 2 -s" "$KENCODER_SESSION_TEST_CALLS"; then restored=1; break; fi
            sleep 0.1
        done
        if [[ $restored != 1 ]]; then
            cat "$work/private-session.log" >&2
            echo "FAIL: did not restore configured Default" >&2
            exit 1
        fi
        mapfile -t calls < "$KENCODER_SESSION_TEST_CALLS"
        [[ ${#calls[@]} == 2 && ${calls[0]} == "-o" && ${calls[1]} == "-b 2 -s" ]]
        dbus-send --session --print-reply --dest=org.kencoder.Notify1 /org/kencoder/Notify1 org.kencoder.Notify1.ListenerStatus > "$work/listener-status.log"
        grep -Fq "string \"disabled\"" "$work/listener-status.log"
        dbus-send --session --print-reply --dest=org.kencoder.Notify1 /org/kencoder/Notify1 org.kencoder.Notify1.MuteProtocolVersion > "$work/mute-protocol.log"
        grep -Fq "uint32 1" "$work/mute-protocol.log"
        dbus-send --session --print-reply --dest=org.kencoder.Notify1 /org/kencoder/Notify1 org.kencoder.Notify1.NotificationsMuted > "$work/muted-before.log"
        grep -Fq "boolean false" "$work/muted-before.log"
        printf "%s\n" "{\"version\":1,\"muted\":true}" > "$XDG_CONFIG_HOME/kencoder/notify-muted.json"
        dbus-send --session --print-reply --dest=org.kencoder.Notify1 /org/kencoder/Notify1 org.kencoder.Notify1.Reload > "$work/mute-reload.log"
        grep -Fq "boolean true" "$work/mute-reload.log"
        dbus-send --session --print-reply --dest=org.kencoder.Notify1 /org/kencoder/Notify1 org.kencoder.Notify1.NotificationsMuted > "$work/muted-after.log"
        grep -Fq "boolean true" "$work/muted-after.log"
    ' bash "$bin" "$work"
else
    printf 'SKIP: dbus-run-session unavailable\n'
fi
printf '==> Malformed policy must be rejected without HID\n'
printf '{\n' > "$XDG_CONFIG_HOME/kencoder/session-policy.json"
if "$bin" --session-policy > "$work/rejected.log" 2>&1; then
    echo 'FAIL: malformed session policy accepted' >&2
    exit 1
fi
printf 'PASS: session policy mock test, no HID; log: %s\n' "$log"

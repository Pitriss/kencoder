# KEncoder Notify

## Overview

KEncoder Notify provides configurable keyboard-lighting reactions to KDE Plasma 6 desktop notifications.

The implementation consists of:

- `kencoder-notify`: per-user Qt 6 daemon and lighting sequence player
- `kcm_kencoder_notify`: Plasma 6 configuration module
- Shared configuration validation and persistence in `notify/config.cpp`
- Session lighting policy handling in `notify/session_policy.cpp`

The daemon starts automatically with the KDE Plasma session through an XDG autostart entry. It remains active even when notification listening is disabled because it also manages keyboard lighting during session lock and monitor-power transitions.

Notification listening is opt-in and disabled by default.

## Notification backend

The implemented backend uses the Plasma 6 `org.kde.NotificationManager.RegisterWatcher` API on the user's session bus.

The listener:

- Does not own `org.freedesktop.Notifications`.
- Does not use `dbus-monitor` or inspect arbitrary D-Bus traffic.
- Does not capture keyboard input.
- Registers with Plasma only while notification listening is enabled.

The `knotify-scanner` backend shown as a future option in the KCM is not implemented. It remains disabled and is never selected as an automatic fallback.

The `kencoder-notify` daemon uses Qt 6 Core and DBus and does not link against KF6. The KCM plugin is built separately. The ordinary Qt Widgets `kencoder` executable remains KF6-free.

## Configuration files

Configuration is stored per user under:

`$XDG_CONFIG_HOME/kencoder/`

The daemon and KCM use four independent, versioned JSON files:

| File | Purpose |
|---|---|
| `notify-presets.json` | Saved lighting presets and their ordered steps |
| `notify-rules.json` | Notification matching rules, listener enablement and backend |
| `notify-muted.json` | Manual mute state for automatic lighting reactions |
| `session-policy.json` | Session lock, monitor-power and inactive-notification policy |

Writes are atomic. Missing files use safe defaults. Invalid notification configuration fails closed rather than silently retaining stale rules.

### Presets and steps

Presets contain ordered lighting steps. Supported step types are:

- `static`
- `off`
- `breathe`
- `ripple`
- `left_wave`
- `right_wave`
- `wait`

Brightness uses the CLI scale 0–3. Animation speed is stored as 1 (slow), 2 (medium) or 3 (fast). Step duration is a hold time after the CLI command exits, not a guaranteed hardware animation duration. Supported hold values are 50–60000 ms.

There is no fixed limit on the number of saved presets or steps.

Example `notify-presets.json`:

```json
{
  "version": 1,
  "presets": [
    {
      "id": "teams-two-waves",
      "name": "Teams: two waves",
      "steps": [
        {"type": "left_wave", "brightness": 1, "speed": 2, "duration_ms": 200},
        {"type": "right_wave", "brightness": 1, "speed": 2, "duration_ms": 200}
      ]
    }
  ]
}
```

### Default preset

The reserved preset `Default` (`id=default`) specifies the desired lighting state after a notification reaction. It cannot be deleted.

Only its first step is used and its duration is ignored. If an older configuration has no `Default`, a static brightness-1 preset is synthesized in memory. Reading the older file does not modify it; the synthesized preset is persisted on the next preset save.

When a session off-policy is active, the daemon uses the `off` state instead of `Default` until all applicable off conditions have cleared.

### Notification rules

Rules match literal substrings, case-insensitively:

- `app_contains`: notification application name
- `title_contains`: notification title
- `text_contains`: notification title and body text

Empty conditions mean “any”; at least one condition must be non-empty. A rule references one preset, and multiple rules may reference the same preset.

Rules are evaluated in their stored order. The first enabled, matching rule that is not in cooldown and whose preset can be queued wins. Later matching rules are not triggered for the same notification.

The KCM supports immediate per-rule enable/disable and drag-and-drop ordering. The order is persisted to the JSON array when the drag operation is completed. Up/down controls remain available as an alternative.

Notification replacements and updates (`replaces_id != 0`) are ignored. Each rule has an independent cooldown; the default is 5000 ms.

Example `notify-rules.json`:

```json
{
  "version": 1,
  "enabled": false,
  "backend": "kde-watcher",
  "rules": [
    {
      "id": "teams-xyz",
      "name": "Teams message contains XYZ",
      "enabled": true,
      "match": {
        "app_contains": "Teams",
        "title_contains": "",
        "text_contains": "XYZ"
      },
      "preset_id": "teams-two-waves",
      "cooldown_ms": 5000
    }
  ]
}
```

The listener remains disabled until the user explicitly enables it in the KCM and applies the listener settings.

### Manual mute

The standalone KEncoder application can mute automatic lighting reactions using `notify-muted.json`:

```json
{
  "version": 1,
  "muted": true
}
```

The default is `false`. Muting does not disable KDE desktop notifications, change saved rules or disable the listener. It suppresses automatic lighting reactions. Manual preset playback remains available.

When mute is enabled, queued automatic reactions are cancelled. If a sequence is in progress, the daemon attempts to restore the current `Default` or session-guarded `off` state.

## Playback and restoration

Only validated step types are translated into existing `encoder` CLI arguments. No arbitrary HID opcode is accepted from configuration.

One sequence runs at a time. Up to 16 pending executions may be queued; additional requests are dropped when the queue is full.

After every non-`Default` sequence, the first step of `Default` is applied once without a hold time. If a session off-policy is active, the guarded `off` state takes precedence.

If an effect command fails:

- Remaining steps in the sequence are cancelled.
- Pending sequences are discarded.
- One best-effort restoration of `Default` or the guarded `off` state is attempted.
- A failed restoration is reported and is not retried automatically.

If both an effect and its restoration fail, the physical keyboard state may remain unknown. No software-only guarantee is possible when the HID interface or device is unavailable.

No hardware readback is performed after each sequence step. A successful CLI exit is not proof of the resulting physical lighting state.

Manual `--play` requests forwarded to an already-running daemon require playback protocol version 2 or newer. An older or unresponsive daemon is rejected rather than silently accepting playback without the restoration behavior. Restart the daemon after updating its executable. A forwarded request acknowledges queueing, not final hardware completion.

## KCM user interface

The KEncoder Notify KCM is grouped under **System Settings → Keyboard**, next to the main HP OMEN Encoder module.

The KCM provides:

- Listener enable/disable and backend selection
- A separate button to apply listener settings
- Preset creation, editing, playback and deletion
- A protected `Default` preset
- Editable steps with compact, aligned controls for effect, brightness, speed and duration
- Independently scrolling Presets and Rules sections
- A vertical splitter that resizes the two sections up and down
- Drag-and-drop rule ordering with edge autoscroll
- Up/down rule ordering controls as an alternative
- Per-rule enable checkboxes
- A persistent status bar for listener state, backend information, playback, configuration and errors

The preset editor can be enlarged but cannot be reduced below its opening dimensions. The listener selector has no redundant Backend label.

## Daemon and diagnostic commands

The daemon is installed at:

`/usr/bin/kencoder-notify`

It is started once per KDE Plasma session by:

`/etc/xdg/autostart/kencoder-notify-autostart.desktop`

It runs even when notification listening is disabled because session policies are independent of notification rules. Installing the package during an already-running Plasma session does not retroactively execute the new autostart entry; log out and back in, or start the daemon manually as the logged-in user.

Useful commands:

```bash
kencoder-notify --check
kencoder-notify --daemon
kencoder-notify --simulate APP TITLE BODY
kencoder-notify --play PRESET_ID --dry-run
```

`--check`, `--simulate`, and `--play ... --dry-run` do not access keyboard HID hardware. Plain `--play` sends commands to the real keyboard, either directly or through a compatible running daemon.

The daemon uses the `encoder` executable found in the user's `PATH`. Do not run it as root.

## Privacy and safety

Notification titles and bodies are not stored in JSON files or diagnostics. Avoid logging notification payloads.

The listener uses the existing `encoder` CLI. It does not introduce new HID commands, inspect keyboard input interfaces or archive notification contents.

The queue is bounded, matching rules have cooldowns, and the listener is disabled by default.

The Plasma notification watcher API does not guarantee long-term ABI/API stability. If registration fails, the listener remains unavailable rather than switching to an undocumented backend.

## Build and tests

```bash
cmake -S notify -B .tmp/kencoder-notify-build -DCMAKE_BUILD_TYPE=Debug
cmake --build .tmp/kencoder-notify-build
cmake -S gui -B .tmp/kencoder-notify-gui-build -DKENCODER_REQUIRE_KCM=ON
cmake --build .tmp/kencoder-notify-gui-build
```

Run configuration and playback diagnostics in an isolated configuration directory:

```bash
XDG_CONFIG_HOME="$PWD/.tmp/notify-sandbox" ./.tmp/kencoder-notify-build/kencoder-notify --check
XDG_CONFIG_HOME="$PWD/.tmp/notify-sandbox" ./.tmp/kencoder-notify-build/kencoder-notify --play teams-two-waves --dry-run
XDG_CONFIG_HOME="$PWD/.tmp/notify-sandbox" ./.tmp/kencoder-notify-build/kencoder-notify --simulate Teams Chat XYZ
```

The no-HID regression suite is:

```bash
bash ./tests/test-notify-nohid.sh
```

It uses isolated configuration directories, private D-Bus sessions and mock `encoder` commands. It does not require a real keyboard.

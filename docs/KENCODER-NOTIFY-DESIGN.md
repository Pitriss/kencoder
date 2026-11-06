# KEncoder Notify: experimental design (branch `feature/notification-reactions`)

## Scope of the first implementation

- **Opt-in** per-user notification reaction system, disabled by default.
- Primary backend: Plasma 6 `org.kde.NotificationManager.RegisterWatcher` on the user session bus. It does **not** own `org.freedesktop.Notifications`, and it does not use `dbus-monitor` or capture keyboard-input interfaces.
- Planned backend `knotify-scanner` / per-user scanner is **phase 2**, shown but disabled in the KCM. Never silently use it as a fallback.
- Separate `kencoder-notify` process uses Qt6Core and Qt6DBus, not KF6.
- Separate `kcm_kencoder_notify` plugin inside the existing optional KDE KCM build. The ordinary Qt Widgets `kencoder` executable remains KF6-free.
- Two independent, atomically written, versioned JSON files under the XDG config home:
  - `$XDG_CONFIG_HOME/kencoder/notify-presets.json`
  - `$XDG_CONFIG_HOME/kencoder/notify-rules.json`
- No hard-coded maximum number of saved presets, rules or steps. To prevent hardware-command floods, only 16 *pending executions* may be queued at a time; overflow is dropped. One sequence runs at a time.
- Rules match literal substrings, case-insensitively: `app_contains`, `title_contains`, `text_contains` (title + body). Empty strings mean “any”; at least one condition must be non-empty. A rule has one preset; multiple rules may reference the same preset. Rules are evaluated in stored order; the **first enabled, matching, non-cooldown rule that can be queued wins**. Later matching rules do not trigger for that notification. The KCM moves rows with ↑/↓ and immediately persists JSON array order.
- Replacements/updates (`replaces_id != 0`) are ignored to avoid repeated animation from progress notifications. A per-rule cooldown (default 5000 ms) provides further protection.
- Only configured, validated action types map to the existing `encoder` CLI. No unknown HID opcode is sent. Steps: `static`, `off`, `breathe`, `ripple`, `left_wave`, `right_wave`, `wait`.
- Step duration (50..60000 ms) starts **after** the CLI command exits. USB/HID execution timing is not guaranteed. After **every** non-Default sequence, the first step of the reserved `Default` preset is applied once, **without hold time**. If an effect fails, remaining effects and pending sequences are cancelled, but a single best-effort Default restoration is still attempted. A failed restoration is reported and never retried automatically. To blink twice, explicitly add `off/static/off/static` steps.
- `--play` refuses to forward to an older/unresponsive D-Bus daemon (playback protocol < 2) that could omit restoration. Restart an already running daemon after upgrading; a daemon processing notifications cannot be replaced just by rebuilding a binary. Forwarded `--play` acknowledges queueing, not final hardware completion.
- No `encoder --status` readback follows sequence steps. A successful CLI return is not proof of the resulting physical lighting state. If both an effect and the Default restoration fail, the device may still be in an unknown state; no software-only guarantee is possible when HID or the device is unavailable.
- Messages (title and body) are not saved to JSON or diagnostics. Avoid printing notification payloads to journald.

## User interface

Top of `kcm_kencoder_notify`:

1. `Aktivovat naslouchání notifikacím` checkbox.
2. Backend drop-down: `KEncoder listener (KDE watcher)` and disabled `KNotify scanner / user daemon (fáze 2)`.
3. **Separate** `Použít nastavení naslouchání` button. The draft checkbox/dropdown do not take effect until this button is clicked.

Below: independently scrolling **Presety** and **Pravidla** lists. The first, undeletable preset `Default` (`id=default`) specifies the desired post-reaction lighting state; only its first step is used, with duration ignored. Missing `Default` is synthesized in memory as static brightness 1; reading an old JSON never writes it back automatically. It becomes persisted on the next preset save. Other presets have `Přehrát / Upravit / Smazat` under the list, plus `Nový preset` in the header. Rules have `Upravit / Smazat`, plus `Nové pravidlo` in the header and an immediate, per-row enable checkbox. Preset editor may be enlarged using the mouse but cannot be shrunk below its opening dimensions. Steps can be loaded into the form and edited; brightness/speed use equally sized ComboBoxes in separate rows. A persistent status bar shows listener state, backend hints, saved/failed operations and rule enable/move feedback. The listener selector has no redundant Backend label; the preset editor opens taller to fit the stacked fields.

## JSON example

`notify-presets.json`:

```json
{
  "version": 1,
  "presets": [
    {
      "id": "teams-two-waves",
      "name": "Teams: dvě vlny",
      "steps": [
        {"type":"left_wave",  "brightness":1, "speed":2, "duration_ms":200},
        {"type":"right_wave", "brightness":1, "speed":2, "duration_ms":200}
      ]
    }
  ]
}
```

`notify-rules.json`:

```json
{
  "version": 1,
  "enabled": false,
  "backend": "kde-watcher",
  "rules": [
    {
      "id": "teams-xyz",
      "name": "Teams obsahuje XYZ",
      "enabled": true,
      "match": {"app_contains":"Teams", "title_contains":"", "text_contains":"XYZ"},
      "preset_id": "teams-two-waves",
      "cooldown_ms": 5000
    }
  ]
}
```

Speeds are numbered 1 (slow), 2 (medium), 3 (fast), translated to `encoder -v slow|medium|fast`. Brightness uses the public CLI's 0..3 scale. `duration_ms` is a hold time after each command, not a precise HID duration.

## Build and safety checks

```bash
cmake -S notify -B .tmp/kencoder-notify-build -DCMAKE_BUILD_TYPE=Debug && cmake --build .tmp/kencoder-notify-build
cmake -S gui -B .tmp/kencoder-notify-gui-build -DKENCODER_REQUIRE_KCM=ON && cmake --build .tmp/kencoder-notify-gui-build
XDG_CONFIG_HOME="$PWD/.tmp/notify-sandbox" ./.tmp/kencoder-notify-build/kencoder-notify --check
XDG_CONFIG_HOME="$PWD/.tmp/notify-sandbox" ./.tmp/kencoder-notify-build/kencoder-notify --play teams-two-waves --dry-run
XDG_CONFIG_HOME="$PWD/.tmp/notify-sandbox" ./.tmp/kencoder-notify-build/kencoder-notify --simulate Teams Chat XYZ
```

The first binary performs no HID operation for `--check`, `--help`, `--simulate APP TITLE BODY`, or `--play ... --dry-run`. Plain `--play` **will** send commands to the real keyboard if the `encoder` on PATH is real. KCM `Přehrát` triggers plain `--play` only on click.

The experimental executable is not automatically installed or started at login by this patch. Explicit enable in the KCM starts a per-user process when the executable is reachable in PATH. Production autostart, user service, packaging of a third optional `.deb`, and complete independent-tag CI support are separate release tasks.

### Dependency invariant

The `encoder` package must stay Qt/KF6-free. The normal `kencoder` executable must stay KF6-free and the KDE plugin libraries remain **optional** `Suggests`, not `Depends`. An eventual separate optional `kencoder-notify` package will depend only on the libraries its own binary really uses, and `kencoder` will merely suggest it. The new plugin must be excluded from the `kencoder` shared-library dependency scan just like the original KCM.

## Known limitations and follow-ups

1. **Watcher API is unstable**: KDE warns that this watcher model has no ABI/API stability guarantee. If it disappears, leave the listener inactive rather than sniffing arbitrary D-Bus traffic. Implement a documented phase-2 backend later.
2. **No fully safe flash-write guarantee**: the firmware behavior of repeated setting sequences is not completely characterized. Keep opt-in default, cooldown, sequential execution and backpressure; do not run rapid unattended tests as a default.
3. **No automatic restart across logins yet**: per-user service/desktop autostart belongs to the optional third package, after functional tests.
4. **Desired-state restoration, not readback**: `Default` is user-configured and is not inferred from keyboard hardware. Its first step is sent after the queued sequence completes successfully; an unsuccessful sequence fails closed without a restore write.
5. **Desktop identity**: KDE delivers the notification `app_name`, which can differ from desktop-file ID; the first matching phase uses `app_contains` with case-insensitive substring checks.
6. **No message archiving**: privacy is part of the design. A future “last notification” picker requires an explicit opt-in and memory-only retention.
7. Real Qt6/KF6 build, KCM loading, runtime watcher registration, packaging and hardware command timing must be confirmed on Debian 13; static patch checks do not prove these properties.

## UI refinements (v3)

- The listener's current enabled/disabled state is shown in the status bar only while idle.
  Confirmations, validation failures and transient hints replace the entire status message
  for six seconds, then the listener state returns.
- Preset editor labels (`Efekt`, `Jas`, `Rychlost`, `Trvání`) use a common compact width.
  The step selector and brightness/speed selectors take the remaining row width.
- The preset and rule panels are separated by a **horizontal draggable splitter**.
  Only their height changes; the listener controls above and status bar below stay fixed.
- Rules may be reordered by dragging the grip at the beginning of a row, including
  edge autoscroll for long lists; release persists a single JSON reorder. The ↑/↓
  buttons remain available as an alternative.
- After every successfully executed non-Default sequence, the first Default step is
  sent once with no hold, before processing another queued sequence. Manual `--play`
  forwarded to an already-running daemon reloads its preset/Default values from disk
  before enqueueing. On failed encoder commands, playback stops without restoration.
- The two JSON files retain version 1 and the listener remains disabled by default.

## UI refinements (v4)

- The vertical SplitView retains up/down resizing, but its handle is a thin
  theme-colored horizontal line with a compact center grip and a resize cursor.
  Transparent space above and below the line separates the preset and rule panels.
- The bottom status frame has a small top margin. Listener, playback, rules,
  configuration and hardware semantics remain unchanged from v3.

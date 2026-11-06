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
- Rules match literal substrings, case-insensitively: `app_contains`, `title_contains`, `text_contains` (title + body). Empty strings mean “any”; at least one condition must be non-empty. A rule has one preset; multiple rules may reference the same preset.
- Replacements/updates (`replaces_id != 0`) are ignored to avoid repeated animation from progress notifications. A per-rule cooldown (default 5000 ms) provides further protection.
- Only configured, validated action types map to the existing `encoder` CLI. No unknown HID opcode is sent. Steps: `static`, `off`, `breathe`, `ripple`, `left_wave`, `right_wave`, `wait`.
- Step duration (50..60000 ms) starts **after** the CLI command exits. USB/HID execution timing is not guaranteed. The final lighting state is left as set by the last step; there is no automatic restore. To blink twice, explicitly add `off/static/off/static` steps.
- No `encoder --status` readback follows sequence steps. A successful CLI return is not proof of the resulting physical lighting state.
- Messages (title and body) are not saved to JSON or diagnostics. Avoid printing notification payloads to journald.

## User interface

Top of `kcm_kencoder_notify`:

1. `Aktivovat naslouchání notifikacím` checkbox.
2. Backend drop-down: `KEncoder listener (KDE watcher)` and disabled `KNotify scanner / user daemon (fáze 2)`.
3. **Separate** `Použít nastavení naslouchání` button. The draft checkbox/dropdown do not take effect until this button is clicked.

Below: independently scrolling **Presety** and **Pravidla** lists. Presets have `Přehrát / Upravit / Smazat` under the list, plus `Nový preset` in the header. Rules have `Upravit / Smazat`, plus `Nové pravidlo` in the header. Editors are modal, with visible per-step labels, move-up/move-down buttons and explicit Save/Cancel.

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
4. **No keyboard state restoration** because wave direction and animation speed cannot be read back reliably. Users can append an explicit final `static` step.
5. **Desktop identity**: KDE delivers the notification `app_name`, which can differ from desktop-file ID; the first matching phase uses `app_contains` with case-insensitive substring checks.
6. **No message archiving**: privacy is part of the design. A future “last notification” picker requires an explicit opt-in and memory-only retention.
7. Real Qt6/KF6 build, KCM loading, runtime watcher registration, packaging and hardware command timing must be confirmed on Debian 13; static patch checks do not prove these properties.

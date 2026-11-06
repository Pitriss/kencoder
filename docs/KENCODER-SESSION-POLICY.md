# KEncoder: Plasma session lighting policy

## Overview

KEncoder integrates keyboard lighting with KDE Plasma 6 session events. The original HP OMEN Encoder KCM stores independent per-user options in:

`$XDG_CONFIG_HOME/kencoder/session-policy.json`

The version-1 JSON file contains three booleans:

- `off_on_lock`
- `off_on_screen_off`
- `notify_when_inactive`

A missing file means all policies are disabled. Writes are atomic. Opening the KCM does not change keyboard lighting or enable a policy.

Example:

```json
{
  "version": 1,
  "off_on_lock": true,
  "off_on_screen_off": true,
  "notify_when_inactive": false
}
```

## Session lock

The daemon monitors `org.freedesktop.ScreenSaver.ActiveChanged` on the Plasma session bus.

The current lock state is queried when the daemon starts. This prevents an already locked session from leaving the keyboard illuminated after daemon startup. When `off_on_lock` is enabled, keyboard lighting is turned off while the session is locked.

After unlocking, the first step of the configured `Default` preset is applied once, unless another off-policy is still active.

This is a session lock/unlock event, not a separate screen-saver animation event.

## Monitor power state

Monitor power is checked every three seconds.

- On Wayland, the daemon uses `kscreen-doctor --dpms show`.
- On X11, it uses `xset q`.

The session is considered screen-off only when all queried monitors report DPMS off. Detection may lag by up to three seconds. If the required command is unavailable or its output cannot be parsed, screen-off detection cannot be confirmed.

This policy does not turn monitors off. It reacts to their reported power state.

## Notification behavior

By default, notification reactions are ignored while the session is locked or while the monitors report off.

When `notify_when_inactive` is enabled, notification reactions are allowed during those states. If `off_on_lock` or `off_on_screen_off` is also active, the daemon restores the guarded `off` state after the notification sequence instead of leaving the keyboard illuminated.

The standalone KEncoder application's separate mute switch suppresses automatic lighting reactions without changing KDE desktop notifications, saved notification rules or listener enablement. Manual preset playback remains available.

## Suspend and resume

A per-user process cannot handle events while Linux is actually suspended. The inactive-session option applies to locked or screen-blanked sessions, not to suspend.

After resume, the daemon re-evaluates the current session and monitor state. If an off-policy is no longer active after wake/unlock, it applies the first step of the configured `Default` preset once, without a duration.

If the session is unlocked while monitors are still off and `off_on_screen_off` is enabled, unlocking alone does not restore the Default lighting state.

## Daemon lifecycle

The session worker is:

`/usr/bin/kencoder-notify --daemon`

It starts automatically through:

`/etc/xdg/autostart/kencoder-notify-autostart.desktop`

The daemon runs once per user's KDE Plasma session and uses that session's D-Bus connection. It runs even when notification listening is disabled because lock and monitor-power policies are independent of notification rules.

Installing the package while Plasma is already running does not execute the new XDG autostart entry in the current session. Log out and back in, or start the daemon manually as the logged-in user:

```bash
kencoder-notify --daemon
```

When a policy is changed in the KCM, the configuration is saved and the running daemon is asked to reload it. The KCM checks `SessionPolicyProtocolVersion`; an older daemon is rejected rather than silently ignoring the new policy. Restart an already-running daemon after updating its executable.

## Readiness indicators

The original HP OMEN Encoder KCM displays three independent readiness indicators.

### Keyboard

Green only after a successful `encoder --status --json` hardware readback, amber while checking and red on failure.

The readback reports the device-reported brightness and effect family. It cannot determine wave direction or animation speed. The current parser accepts the validated Woody2 protocol profile. The readback is a separate, bounded HID query; session policy monitoring itself does not continuously poll the keyboard.

### KEncoder Notify

Checks the `kencoder-notify` executable, notification configuration and the running daemon's D-Bus protocol. A valid but stopped daemon is shown as not currently running. Invalid configuration or an incompatible daemon is reported as a problem.

### Listener

Checks the daemon's actual registration state with the Plasma notification manager:

- **Active:** the notification watcher is registered.
- **Disabled:** notification listening is disabled in KEncoder Notify settings.
- **Unavailable:** listening is enabled, but registration with Plasma failed.

The listener can be disabled while session lighting policies remain operational.

## Standalone KEncoder application

The Qt Widgets application provides:

- Runtime keyboard readiness status
- A switch to mute automatic KEncoder Notify lighting reactions
- Direct access to the KEncoder System Settings module
- Optional system-tray integration

The mute switch affects only automatic KEncoder Notify lighting reactions. It does not disable KDE desktop notifications.

The **Otevřít ovládací panel** button prefers `systemsettings kcm_kencoder`, then `systemsettings6 kcm_kencoder`, and finally `kcmshell6 kcm_kencoder`.

## Safety and migration

An in-flight encoder command is allowed to finish or time out before a session transition overrides it. Remaining animation steps are cancelled; the latest off or Default state takes priority.

Session policy uses the existing `encoder` CLI. It adds no new HID protocol and performs no continuous hardware readback.

Stop any older `backlightctl.sh` process and disable its autostart before enabling the equivalent KEncoder policy. Two independent processes issuing lighting commands can overwrite each other's state.

## Testing

Run the session-policy test:

```bash
bash ./tests/test-session-policy-nohid.sh
```

The complete notification and session-policy suite is:

```bash
bash ./tests/test-notify-nohid.sh
```

Tests use private D-Bus sessions and mock executables for `encoder` and `kscreen-doctor`. They do not require real keyboard HID access.

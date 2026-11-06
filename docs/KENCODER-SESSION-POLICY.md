# KEncoder: Session lighting policy (experimental)

The original **HP OMEN Encoder** KCM stores independent per-user options in
`$XDG_CONFIG_HOME/kencoder/session-policy.json` (`version=1`, booleans
`off_on_lock`, `off_on_screen_off`, `notify_when_inactive`). Missing file means
all options disabled. Writes are atomic, and opening the KCM does not modify
lighting or enable any new policy.

- **Lock:** `org.freedesktop.ScreenSaver.ActiveChanged` on the Plasma session bus;
  `GetActive` is queried when the service starts, so launching while locked
  does not leave the keyboard illuminated. KScreenLocker emits this event on
  lock/unlock; it is not a separate "screen saver animation" event.
- **Screen saver / screen off:** monitor **DPMS** using `kscreen-doctor --dpms show`
  every three seconds on Wayland, `xset q` on X11. Only if all queried monitors
  are off is the session treated as screen-off. Detection may lag up to three
  seconds; if the query fails or neither tool is present, this option cannot
  detect screen-off. It does not force the monitor to power off.
- **Notification gating:** by default, KEncoder Notify ignores notifications
  while locked or while the monitors report off. When explicitly permitted, it
  may play an effect while inactive; if the corresponding off-policy is also
  active, the effect is restored to the `off` state instead of lighting the
  keyboard continuously.
- **Suspend:** Linux suspend stops execution of this per-user process. Events
  cannot be processed while the computer is truly suspended. The checkbox
  controls notifications while locked/screen-blanked, not while suspended.
- **Wake/unlock:** after KEncoder automatically turned the lighting off,
  the controller applies the **first step of the configured `Default` preset**
  once, without duration. When both policies are applicable, unlocking alone
  will not light the keyboard while the screen is still off.
- **Safety:** an in-flight CLI command is allowed to finish (or time out) before
  the session transition overrides it. Remaining animation steps are cancelled;
  the latest off/Default state wins. All commands continue to use the known
  `encoder` CLI; this feature adds no HID operations or polling of the keyboard.
  Independent readback is not part of the session policy.

The existing `kencoder-notify --daemon` is the session worker and is installed
with a Plasma XDG autostart entry. It runs even if notification listening is
disabled, because lock/DPMS rules are independent. Enabling a checkbox tries
to start or reload it immediately. Installing a package requires a new login
(or manual `kencoder-notify --daemon` invocation) to activate the autostart.

The KCM checks `SessionPolicyProtocolVersion` before reloading a running
worker; an older listener is rejected rather than silently ignoring options.

## Readiness and manual mute

The original KEncoder KCM presents a read-only three-light readiness panel:
1. Keyboard status is green only after a successful `encoder --status --json`
   readback, amber while checking, red on failure. Checking this status uses
   the existing validated read-only HID command, not a lighting write.
2. KEncoder Notify readiness checks the executable, valid configuration and
   the running daemon protocol. A configured but stopped daemon is amber.
3. Listener readiness asks the running daemon whether the Plasma watcher was
   actually registered. Disabled is amber; failed registration is red.

The standalone Qt Widgets GUI has a **Vypnout světelné notifikace KEncoder Notify**
checkbox. It atomically stores a separate XDG file,
`$XDG_CONFIG_HOME/kencoder/notify-muted.json`, format
`{"version":1,"muted":true|false}`. The default is false. Muting suppresses
new automatic lighting reactions without altering the KDE notification system,
existing rules, or listener enablement. Manual preset playback remains possible.
The daemon observes this file and on mute cancels queued effects; if an effect
was in progress, it attempts to restore the current Default/off policy.
An older daemon does not understand this file and must be restarted after update.

The **Otevřít ovládací panel** button prefers `systemsettings kcm_kencoder`,
then `systemsettings6 kcm_kencoder`, then `kcmshell6 kcm_kencoder`.

**Migration:** Stop the old `backlightctl.sh` process before activating a session
policy, otherwise both processes will issue conflicting `encoder` commands.

**Testing:** `bash ./tests/test-notify-nohid.sh` now includes an isolated private
D-Bus test using mock executables for `encoder` and `kscreen-doctor`. No real
HID access is requested by the test suite.

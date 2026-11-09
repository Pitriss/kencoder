# KEncoder

KEncoder is a Linux application for controlling the RGB backlighting of the HP OMEN Encoder keyboard (USB ID `03f0:3341`).

The project is a fork of [encoder_keyboard](https://github.com/player999/encoder_keyboard) by Taras Zakharchenko. It extends the original command-line utility with a Qt 6 desktop application and two KDE Plasma 6 System Settings modules.

## Features

### Encoder CLI

- Static backlighting and lighting off
- Brightness control (levels 0–3)
- Animation modes: Ripple, Left Wave, Right Wave and Breathing
- Animation speed: Slow, Medium and Fast
- USB HID communication and udev rules
- Read-only hardware status query: `encoder --status`
- Machine-readable status: `encoder --status --json`
- Manual page: `man encoder`

The hardware readback reports the device-reported brightness and effect family. It cannot determine wave direction or animation speed. The current parser accepts the validated Woody2 protocol profile.

### KEncoder desktop application

- Native Qt 6 interface for KDE Plasma 6
- Animation, brightness and speed controls
- Read-only hardware status and runtime readiness monitoring
- Optional minimize-to-system-tray behavior
- Manual application of the saved `Default` lighting preset
- Switch to mute automatic KEncoder Notify lighting reactions
- Direct access to the KEncoder System Settings module
- Status messages for applied commands and errors

Opening the application does not automatically change keyboard lighting.

### KDE Plasma 6 System Settings

Both KCM modules are grouped under **System Settings → Keyboard**.

**HP OMEN Encoder** provides:

- Manual lighting configuration and `Default` preset application
- Hardware status readback
- Three readiness indicators for the keyboard, KEncoder Notify service and notification listener
- Automatic lighting off on session lock
- Automatic lighting off when all monitors report DPMS off
- Optional notification reactions while the session is locked or the monitors are off
- A shortcut to launch the standalone KEncoder application

**KEncoder Notify** provides:

- Opt-in Plasma notification listening
- Editable lighting presets and animation steps
- Ordered notification matching rules
- Per-rule enable/disable and cooldown
- Drag-and-drop rule ordering
- Automatic restoration of the configured `Default` state
- A persistent status bar and playback feedback

### Session integration

The `kencoder-notify` per-user daemon starts with the KDE Plasma session through `/etc/xdg/autostart/kencoder-notify-autostart.desktop`.

The daemon manages lock and monitor-power policies independently of notification listening. It monitors Plasma session lock/unlock events and monitor DPMS state. After unlock or monitor wake, it restores the first step of the configured `Default` preset when no off-policy remains active.

A process in a user session cannot handle notification events while Linux is actually suspended. The inactive-session option applies to locked or screen-blanked sessions, not to suspend.

### Notification reactions

KEncoder Notify uses the Plasma 6 notification watcher API. Rules match application name, notification title and body text using case-insensitive literal substrings. The first eligible matching rule wins. Notification replacements are ignored and per-rule cooldowns limit repeated reactions.

Presets run sequentially. After each non-`Default` sequence, the first step of `Default` is applied once without a hold time. The execution queue is bounded to avoid flooding the keyboard with commands.

Notification contents are not archived in configuration files or diagnostics. The separate mute switch suppresses automatic lighting reactions without disabling KDE desktop notifications, changing rules or disabling the listener.

## Build from source

Requirements for the CLI: Linux, GCC, Make and the usual C build tools.

The desktop application requires Qt 6 Widgets and Qt 6 DBus. Building the Plasma modules additionally requires ECM, Qt 6 Quick/QML and KF6 KCMUtils development packages.

### CLI

```bash
make -j"$(nproc)"
./build/cli/encoder -h
./build/cli/encoder --status --json
```

### Desktop application and Plasma KCM modules

```bash
cmake -S gui -B build/gui -DKENCODER_REQUIRE_KCM=ON
cmake --build build/gui -j"$(nproc)"
PATH="$PWD/build/cli:$PATH" ./build/gui/kencoder
```

### Notification and session daemon

```bash
cmake -S notify -B build/notify
cmake --build build/notify -j"$(nproc)"
PATH="$PWD/build/cli:$PATH" ./build/notify/kencoder-notify --check
PATH="$PWD/build/cli:$PATH" ./build/notify/kencoder-notify --daemon
```

The daemon uses the current user's Plasma session D-Bus connection and the `encoder` executable found in `PATH`. Do not run it as root.

### Tests

```bash
bash ./tests/test-notify-nohid.sh
bash ./tests/test-session-policy-nohid.sh
```

The tests use isolated configuration directories, private D-Bus sessions and mock executables. They do not require access to the real keyboard HID interface.

## Debian packages

The project provides two packages:

- `encoder` — command-line utility, hardware readback and udev rules
- `kencoder` — Qt 6 desktop application, `kencoder-notify` daemon, both Plasma KCM modules and the Plasma XDG autostart entry

The `kencoder` package depends on a compatible `encoder` version and the runtime libraries needed by its application and KCM modules.

GitHub Actions builds Debian 13 packages for amd64 and arm64. CLI and GUI releases are versioned independently:

- `encoder-vX.Y.Z` builds the CLI package.
- `kencoder-vX.Y.Z` builds the GUI package and reuses the latest published CLI packages after verifying their checksums and dependency compatibility.

## Documentation

- [KEncoder Notify](docs/KENCODER-NOTIFY-DESIGN.md)
- [Plasma session lighting policy](docs/KENCODER-SESSION-POLICY.md)
- [Encoder protocol notes](docs/OMEN-ENCODER-HID-PROTOCOL.md)

## Translations

The Qt Widgets application and both Plasma 6 KCM modules share a single
gettext template (`translations/kencoder.pot`). Czech is the source language;
English, Slovak, Ukrainian, German, Russian and Polish are shipped as
editable PO catalogs and compiled into dynamically loaded Qt QM files.

See [the English translation guide](translations/README.md) for adding messages,
updating the catalogs, testing translations and overriding language files.

### Localized encoder(1) manual pages

The encoder(1) manual is maintained in Czech as the canonical
reference, with complete translations available in six languages:

- English (en)
- Slovak (sk)
- Ukrainian (uk)
- German (de)
- Russian (ru)
- Polish (pl)

All translated manual pages are included in the Debian packaging
of the standalone `encoder` CLI component, not `kencoder`.

During package builds, Debian installs localized manual pages under
`/usr/share/man/<language>/man1/encoder.1.gz`. The original Czech
manual remains the default.

The translated pages cover the complete command reference,
including options, examples, HID permissions, status readback,
option precedence, known limitations, and exit codes.

Localized manual pages are included in the standalone `encoder` Debian
package starting with version 0.2.1. The CLI commands and HID behavior
remain unchanged from 0.2.0.

See docs/man/README.md for translation maintenance and
verification instructions.

## License

BSD 3-Clause License.

Original encoder code: Copyright (c) 2020 Taras Zakharchenko.

KEncoder additions and modifications: Copyright (c) 2026 Pitriss.

See [LICENSE](LICENSE) and [debian/copyright](debian/copyright) for details.

## Acknowledgements

**A huge thank you to Taras Zakharchenko**, the original author of [encoder_keyboard](https://github.com/player999/encoder_keyboard), for creating and sharing the software that made this project possible.

**AI disclosure:** The KEncoder-specific code and modifications in this fork were generated using AI tools, with human direction, testing and review.

The original author's work and contribution remain fully credited.

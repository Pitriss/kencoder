# KEncoder: Qt 6 GUI + original encoder CLI

Fork of [player999/encoder_keyboard](https://github.com/player999/encoder_keyboard),
with KDE Plasma 6-friendly Qt 6 Widgets GUI and native Debian 13 packages.

## Packages

| Package | Contents | Dependencies |
| --- | --- | --- |
| `encoder` | `/usr/bin/encoder`, restrictive udev rule | udev, libc |
| `kencoder` | `/usr/bin/kencoder`, application menu entry | `encoder`, Qt 6 Widgets |

Supported device: **HP OMEN Encoder**, USB **03f0:3341**.
The tool does not support arbitrary keyboard models.

Run `kencoder` from the Plasma menu or a terminal. GUI runs `encoder`
with explicit argument vectors, without a shell or privilege elevation.
The Debian package's udev rule uses `TAG+="uaccess"` and `MODE="0660"`,
with priority 99 to override the insecure upstream priority-97 rule.
After installing `encoder`, reconnect the keyboard (or log out and back in)
to allow the device ACL to be refreshed. Run `udevadm info` and check
`getfacl /dev/hidrawX` if permission problems persist. Never run the GUI as root.

## Build on Debian 13

```bash
sudo apt-get update && sudo apt-get install -y build-essential cmake qt6-base-dev debhelper fakeroot dpkg-dev
```

```bash
dpkg-buildpackage -b -us -uc
```

Two `.deb` packages are created in the parent directory. Install both
with `sudo apt-get install ./encoder_*.deb ./kencoder_*.deb` from that directory.

For a CLI-only build, `make` builds the upstream-compatible `encoder` command.
For a standalone GUI build, use:

```bash
cmake -S gui -B build/gui && cmake --build build/gui --parallel
```

The command `QT_QPA_PLATFORM=offscreen ./build/gui/kencoder --self-test`
smoke-tests GUI creation without hardware or a graphical display.
This does **not** validate USB protocol behavior.

## CI / Releases

`.github/workflows/debian-release.yml` builds on native amd64 and arm64 runners,
inside **Debian 13 (`debian:trixie`)** containers. The workflow is manual or
triggered by tags `v*`, not by every push.

- On a manual run, two artifact bundles are uploaded (one per architecture).
- On a release tag, four `.deb` files and a SHA256SUMS manifest are attached
  to the GitHub release.
- Before a new tag, update `debian/changelog` to match the tag's version.
- Package name/version and ELF architecture are checked during CI.

Example: after testing and committing, push tag `v0.1.0` to publish the
first release. The build has not been tested on real hardware here.

## Licensing

Original C code is BSD-3-Clause, as stated in the upstream source files.
New GUI and packaging use BSD-3-Clause. See `LICENSE` and `debian/copyright`.

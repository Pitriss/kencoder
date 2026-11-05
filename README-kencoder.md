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
The Debian package installs **two udev rules**: `70-encoder-uaccess.rules`
adds the `uaccess` tag before systemd's `73-seat-late.rules` handles ACLs,
while `99-encoder.rules` enforces `MODE="0660"` after the insecure upstream
priority-97 rule. A tag set only in a 99- rule is too late for seat ACLs.
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

## GUI controls

- **Animace:** `Statické` is the default; other choices are Breathe, Left wave, Right wave and Ripple.
- **Jas:** slider with the keyboard's actual hardware levels 0–3. The initial GUI value is **1**, not a verified hardware reading. GUI does not save or restore lighting settings. Startup readback from the USB HID device is **not implemented**, as the upstream protocol documents setting values and reading command acknowledgments, but no tested status query. The tray checkbox preference is still saved independently.
- **Rychlost:** slider with the three firmware choices (Pomalá, Střední, Rychlá). It is disabled in static mode.
- **Nastavit:** applies the selected brightness and lighting mode. **Vypnout:** switches the lighting off.
- **Zavřít do systémového panelu:** when checked and the system tray is available, closing the window hides KEncoder instead of quitting. Click the icon to restore the window; right-click the icon and select **Ukončit** to exit. When unchecked, closing the window exits normally. This choice is saved per user with Qt's QSettings.
- The program icon is embedded in the GUI executable and installed as `kencoder.png` for the desktop launcher and system tray.

A headless widget smoke test (no keyboard required) is available after building:

```sh
QT_QPA_PLATFORM=offscreen ./build/gui/kencoder --self-test
```

## Kontrola stavu v GUI

KEncoder zobrazuje pod nadpisem **Stav** indikátor stavu pod
ovládacími tlačítky. Stav se obnovuje každých 5 sekund a také po dokončení
příkazu. Textové pole pouze pro čtení s názvem klávesnice a nalezenou
cestou `/dev/hidraw*` je zapuštěné úplně dole, bez vnějších okrajů na bocích
a u spodní hrany okna. Ostatní ovládací prvky zachovávají běžné odsazení KDE.
Po příkazu se na 5 sekund zobrazí úspěšná zpráva,
chybová zpráva na 8 sekund; pak se vrátí název a cesta zařízení. Během této
doby průběžná diagnostika zprávu nepřepisuje. Úspěšná zpráva vychází z
návratového kódu CLI a jeho hlášení, není nezávislým ověřením aktuálního
nastavení podsvícení.

- **Zelená**: `encoder` je v PATH, připojené zařízení odpovídá vendor HID
  rozhraní HP OMEN Encoder, jsou nainstalována bezpečná pravidla
  `70-encoder-uaccess.rules` a `99-encoder.rules`, má aktivní značku `uaccess`
  a lze ho otevřít s právy
  pro čtení i zápis.
- **Oranžová**: některé z uvedených udev pravidel chybí, nebo není aplikováno
  (například je nutné klávesnici znovu připojit).
- **Červená**: chybí `encoder`, klávesnice není připojena, zařízení není
  přístupné nebo selhal poslední příkaz.

Kontrola pouze čte `/sys`, pravidlo udev, stav udev a testuje otevření
`/dev/hidraw*`. Neodesílá HID příkazy a nevyčítá skutečný jas nebo animaci.
Hodnota jasu v GUI je vždy předvolená `1`; nastavení podsvícení se neukládá.

Kliknutí na ikonu v systémovém panelu střídavě schová a zobrazí okno.
Pravé tlačítko otevře nabídku s položkou **Ukončit**.
Balíček `kencoder` již má povinnou závislost na `encoder` stejné verze;
při ručním spuštění GUI se absence CLI přesto oznamuje červeně.

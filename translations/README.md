# Translating KEncoder (GUI)

The source language is **Czech (`cs`)**. All GUI text comes from a single
[`kencoder.pot`](kencoder.pot), shared by the Qt Widgets application and both
Plasma QML KCM plugins. Keep PO translation files here:

- `en.po` — English
- `sk.po` — Slovak
- `uk.po` — Ukrainian (ISO 639-1 language code `uk`; country code `UA`)
- `de.po` — German
- `ru.po` — Russian
- `pl.po` — Polish

GUI sources use `QCoreApplication::translate("KEncoder[...]")` (C++) or
`qsTr("...")` (QML). Do not translate JSON keys, preset IDs, mode names,
D-Bus names or command-line arguments.

## Prerequisites (Debian 13)

```bash
sudo apt-get install qt6-l10n-tools gettext
```

## Extract new messages or update existing PO files

From the repository root:

```bash
bash ./translations/update-catalog.sh
```

This extracts all three GUI components using Qt `lupdate`, converts the
combined catalog to one `kencoder.pot` using `lconvert`, then uses `msgmerge`
(or `msginit` for a new language). The script does not touch source code,
commit changes, or access hardware. The only temporary files are created
under the existing `.tmp` directory.

Edit `translations/<locale>.po` with Poedit, Lokalize or a text editor.
Preserve `%1`/`%2` placeholders and avoid altering HTML markup. `msgstr ""`
means untranslated: Qt falls back to the original Czech text.

Run validation after editing PO files:

```bash
for f in translations/{en,sk,uk,de,ru,pl}.po; do msgfmt --check --check-format -o /dev/null "$f" || exit; done
```

## Qt context conversion

Source POT/PO files use normal GNU gettext contexts, for example
`msgctxt "KEncoder"`. During the build, `po-contexts.sh` writes a temporary
Qt-compatible PO file with `X-Qt-Contexts: true` and `msgctxt "KEncoder|"`.
Qt `lconvert` otherwise mistakes the gettext context for a disambiguation,
so it would compile a QM file with no matching C++ or QML contexts.

The source PO/POT files are never rewritten by the CMake build. The
`update-catalog.sh` extractor also normalizes Qt contexts back to gettext
format before calling `msgmerge`, keeping previous translations intact.

## Compile and test QM translations

```bash
cmake -S gui -B .tmp/kencoder-notify-gui-build -DKENCODER_REQUIRE_KCM=ON -DKENCODER_REQUIRE_TRANSLATIONS=ON
cmake --build .tmp/kencoder-notify-gui-build --parallel
```

CMake converts each `*.po` to a temporary Qt `*.ts` and compiles a dynamic
`kencoder_<locale>.qm`. GUI translation files are installed to:

`/usr/share/kencoder/translations/`

No application recompilation is necessary to **replace** installed QM files
with revised translations. Restart the application or KCM to load new files.

The code uses the system's preferred UI language (`QLocale::system()`).
For a single-process preview set `KENCODER_LANG=de`, `KENCODER_LANG=en`, etc.
For Ukrainian use `KENCODER_LANG=uk`. The familiar country code `ua` is also
accepted as an override alias (`KENCODER_LANG=ua`) and selects the same
`kencoder_uk.qm`; no second `ua.po` or `kencoder_ua.qm` is needed.
KDE/Qt desktop metadata retains the standards-compliant `[uk]` locale key.
The optional `KENCODER_TRANSLATIONS_DIR` override searches a directory of
QM files before the user/system standard XDG data paths. A user can override
translations in `~/.local/share/kencoder/translations/` without root access.

Example locally built Widgets preview:

```bash
KENCODER_LANG=de KENCODER_TRANSLATIONS_DIR="$PWD/.tmp/kencoder-notify-gui-build/translations" ./.tmp/kencoder-notify-gui-build/kencoder
```

System Settings metadata (`*.desktop`, KPlugin `*.json`) has its own KDE
localization keys because those entries are displayed before a KCM is loaded.
The GUI itself uses one POT. KDE metadata strings are mirrored in their
native Desktop Entry and KPlugin formats.

## Man pages

Six independently translated CLI man pages live in
`docs/man/<locale>/man1/encoder.1`. They are **not installed by the GUI package**
and are **not added to the `encoder` package yet**. These are pending review
and a later independent CLI release. Preview one without changing any package:

```bash
man -l docs/man/de/man1/encoder.1
```

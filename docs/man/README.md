# Localized `encoder(1)` manual pages

The authoritative manual is the Czech source at [`man1/encoder.1`](man1/encoder.1).
The following six translations mirror its complete command-line reference:

| Language | Source file |
| --- | --- |
| English | `en/man1/encoder.1` |
| Slovak | `sk/man1/encoder.1` |
| Ukrainian | `uk/man1/encoder.1` |
| German | `de/man1/encoder.1` |
| Russian | `ru/man1/encoder.1` |
| Polish | `pl/man1/encoder.1` |

The manual language code for Ukrainian is **`uk`** (ISO 639-1); `UA` is a
country code. The GUI accepts `KENCODER_LANG=ua` as an alias, but this does not
rename the manual-page source file.

## Translation revision (2026-11-09)

The initial 84-line translated summaries have been expanded into complete
references aligned with the Czech manual. The revision restores the full option
semantics, all six original examples, option-precedence rules, the exact HID
error text, permissions and reconnect guidance, and caveats about exit codes
and hardware status readback. No CLI behavior was changed. Details beyond the
Czech source, previously included in abbreviated translations, are not treated
as an independent specification.

## Scope and maintenance

When the Czech manual changes, update all six translations. Preserve the same
sections, option descriptions, examples, and caveats about hardware readback,
permissions, option precedence, and exit statuses. Leave the CLI arguments,
animation identifiers, JSON field names, HID error message, file paths, and
`man(7)` formatting macros unchanged. The command-line interface is not
translated; only its documentation is.

The translations are included in the **`encoder` Debian package**, not in
`kencoder`. Both the regular Debian build and the independent CI release build
prepare `encoder.<lang>.1` files for `dh_installman`. Debian installs them under
`/usr/share/man/<lang>/man1/encoder.1.gz`; the original Czech manual remains
the default at `/usr/share/man/man1/encoder.1.gz`.

The CI package check verifies all seven manual pages. Changes in the source
tree do not alter packages already published; translated manuals become
available when a new `encoder` package is built and released.

## Review without installation

From the repository root, render each translated manual individually:

```bash
man -l docs/man/en/man1/encoder.1
man -l docs/man/sk/man1/encoder.1
man -l docs/man/uk/man1/encoder.1
man -l docs/man/de/man1/encoder.1
man -l docs/man/ru/man1/encoder.1
man -l docs/man/pl/man1/encoder.1
```

To check the render pipeline without opening a pager (requires `groff`):

```bash
for lang in en sk uk de ru pl; do groff -Tutf8 -man "docs/man/$lang/man1/encoder.1" > /dev/null || exit; done
```

Review translated wording as well as roff syntax. Automated checks of section
counts and option names cannot prove linguistic accuracy.

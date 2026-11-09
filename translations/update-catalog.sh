#!/usr/bin/env bash
# Extract C++/QML messages into a single POT; initialize/refresh all six PO files.
# Invoke from repository root: bash ./translations/update-catalog.sh
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
cd "$root"
if [[ -x /usr/lib/qt6/bin/lupdate ]]; then lupdate=/usr/lib/qt6/bin/lupdate
else lupdate=$(command -v lupdate || true); fi
if [[ -x /usr/lib/qt6/bin/lconvert ]]; then lconvert=/usr/lib/qt6/bin/lconvert
else lconvert=$(command -v lconvert || true); fi
for tool in "$lupdate" "$lconvert"; do
    [[ -n "$tool" ]] || { echo 'ERROR: qt6-l10n-tools required' >&2; exit 2; }
done
for tool in msgmerge msginit msgfmt; do
    command -v "$tool" >/dev/null || { echo "ERROR: gettext command $tool required" >&2; exit 2; }
done
# Existing project convention: transient files live inside the preexisting .tmp.
work=$(mktemp -d "$root/.tmp/kencoder-l10n-XXXXXX")
trap 'rm -rf "$work"' EXIT
"$lupdate" \
    gui/main.cpp gui/lighting_status.h \
    gui/kcm/kcm_kencoder.cpp gui/kcm_notify/kcm_kencoder_notify.cpp \
    notify/config.cpp notify/session_policy.cpp \
    gui/kcm/ui/main.qml gui/kcm_notify/ui/main.qml \
    -source-language cs -ts "$work/kencoder.ts"
"$lconvert" -i "$work/kencoder.ts" -if ts -of pot -drop-translations \
    -no-obsolete -locations none -o "$work/kencoder.qt.pot"
# Qt's POT exporter writes Context| entries. Keep our source POT and PO files
# in ordinary gettext format so msgmerge preserves existing translations.
bash ./translations/po-contexts.sh to-gettext \
    "$work/kencoder.qt.pot" translations/kencoder.pot
for lang in en sk uk de ru pl; do
    if [[ -f "translations/$lang.po" ]]; then
        msgmerge --update --backup=none "translations/$lang.po" translations/kencoder.pot
    else
        msginit --no-translator --locale "$lang" -i translations/kencoder.pot \
            -o "translations/$lang.po"
    fi
    msgfmt --check --check-format -o /dev/null "translations/$lang.po"
done
echo 'Updated translations/kencoder.pot and six PO files'

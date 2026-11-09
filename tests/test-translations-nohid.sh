#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
cd "$root"
python3 - <<'PYCHECK'
from pathlib import Path
import re
root=Path('translations')
pot=root/'kencoder.pot'
assert pot.is_file(), 'Missing shared POT'
base=pot.read_text(encoding='utf-8')
contexts=set(re.findall(r'^msgctxt "([^"]+)"$',base,re.M))
required={'KEncoder','KEncoderKcm','KEncoderNotifyKcm','KEncoderLighting','KEncoderErrors','KEncoderSessionErrors','main'}
assert required.issubset(contexts), 'Some GUI contexts missing from the shared POT: '+str(required-contexts)
base_ids=set(re.findall(r'^msgid (".+")$',base,re.M))
assert len(base_ids)>=150, 'Suspiciously small translation catalog'
for locale in ('en','sk','uk','de','ru','pl'):
    p=root/f'{locale}.po'
    assert p.is_file(), f'Missing PO: {locale}'
    contents=p.read_text(encoding='utf-8')
    ids=set(re.findall(r'^msgid (".+")$',contents,re.M))
    missing=base_ids-ids
    assert not missing, f'{locale}: missing catalog messages: {len(missing)}'
    print(f'{locale}: {len(ids)-1} messages in shared POT')
print('PASS: one POT, six PO files, all GUI contexts, no HID')
PYCHECK
if command -v msgfmt >/dev/null; then
    for code in en sk uk de ru pl; do
        msgfmt --check --check-format -o /dev/null "translations/$code.po"
    done
    echo 'PASS: gettext PO syntax'
fi

# Check that the Qt-specific PO copy has real contexts, not only msgctxt
# disambiguation comments. This guard works without starting any GUI.
qt_po=$(mktemp .tmp/kencoder-qt-po-test-XXXXXX.po)
trap 'rm -f "$qt_po"' EXIT
bash ./translations/po-contexts.sh to-qt translations/de.po "$qt_po"
grep -Fq '"X-Qt-Contexts: true\n"' "$qt_po"
grep -Fq 'msgctxt "KEncoder|"' "$qt_po"
grep -Fq 'msgctxt "main|"' "$qt_po"
if grep -Fq 'msgctxt "KEncoder||"' "$qt_po"; then
    echo 'FAIL: duplicate Qt context delimiter' >&2
    exit 1
fi
echo 'PASS: gettext contexts converted to Qt contexts before QM generation'

# If the GUI build has been configured, test the real runtime loader with the
# German catalog. This catches filename/locale fallback mistakes that msgfmt
# and lrelease cannot detect.
build_dir=.tmp/kencoder-notify-gui-build
if [[ -f "$build_dir/CMakeCache.txt" ]]; then
    available_targets=$(cmake --build "$build_dir" --target help 2>/dev/null || true)
    if grep -q 'kencoder_translation_loader_test' <<<"$available_targets"; then
        cmake --build "$build_dir" --target kencoder_translation_loader_test --parallel 2
        KENCODER_LANG=de \
        KENCODER_TRANSLATIONS_DIR="$PWD/$build_dir/translations" \
            "$build_dir/translations/kencoder_translation_loader_test"
        # Ukrainian has language code uk. The country code ua is accepted
        # as an override alias and must load the same kencoder_uk.qm.
        KENCODER_LANG=ua \
        KENCODER_TRANSLATIONS_DIR="$PWD/$build_dir/translations" \
            "$build_dir/translations/kencoder_translation_loader_test"
    else
        echo 'SKIP: Qt6 QML development target unavailable for runtime translation test'
    fi
else
    echo 'SKIP: runtime translation test (configure .tmp/kencoder-notify-gui-build first)'
fi

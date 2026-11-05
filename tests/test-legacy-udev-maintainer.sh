#!/usr/bin/env bash
# Filesystem-only tests for encoder Debian maintainer scripts; never touches host udev.
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
tmp=$(mktemp -d "$project/.tmp/encoder-udev-tests.XXXXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/mock"
cat > "$tmp/mock/dpkg-query" <<'MOCK'
#!/bin/sh
[ "${KENCODER_TEST_OWNED:-0}" = 1 ]
MOCK
cat > "$tmp/mock/udevadm" <<'MOCK'
#!/bin/sh
exit 0
MOCK
chmod 755 "$tmp/mock/dpkg-query" "$tmp/mock/udevadm"
export PATH="$tmp/mock:$PATH"

original='KERNEL=="hidraw*", ATTRS{idVendor}=="03f0", ATTRS{idProduct}=="3341", SUBSYSTEM=="hidraw", MODE="0666", GROUP="users"'

prepare() {
    local name="$1" r="$tmp/$1"
    mkdir -p "$r/etc/udev/rules.d" "$r/usr/lib/udev/rules.d" \
        "$r/usr/local/lib/udev/rules.d" "$r/run/udev/rules.d" \
        "$r/lib/udev/rules.d" "$r/var/lib"
    for kind in postinst postrm; do
        sed -e 's@/etc/udev/rules.d@___KENCODER_ETC___@g' \
            -e 's@/usr/local/lib/udev/rules.d@___KENCODER_LOCAL___@g' \
            -e 's@/usr/lib/udev/rules.d@___KENCODER_USR___@g' \
            -e 's@/lib/udev/rules.d@___KENCODER_LIB___@g' \
            -e 's@/run/udev/rules.d@___KENCODER_RUN___@g' \
            -e 's@/var/lib/encoder@___KENCODER_STATE___@g' \
            "$project/debian/encoder.$kind" | \
        sed -e "s@___KENCODER_ETC___@$r/etc/udev/rules.d@g" \
            -e "s@___KENCODER_LOCAL___@$r/usr/local/lib/udev/rules.d@g" \
            -e "s@___KENCODER_USR___@$r/usr/lib/udev/rules.d@g" \
            -e "s@___KENCODER_LIB___@$r/lib/udev/rules.d@g" \
            -e "s@___KENCODER_RUN___@$r/run/udev/rules.d@g" \
            -e "s@___KENCODER_STATE___@$r/var/lib/encoder@g" > "$r/$kind"
    done
    REPLY=$r
}

mask_is_active() {
    local p="$1/etc/udev/rules.d/97-omen_encoder.rules"
    [[ -L "$p" && "$(readlink "$p")" == /dev/null ]]
}

pass() { printf 'PASS: %s\n' "$1"; }

prepare etc-original; r=$REPLY
printf '%s\n' "$original" > "$r/etc/udev/rules.d/97-omen_encoder.rules"
sh "$r/postinst" configure >/dev/null 2>&1
mask_is_active "$r"
[[ -f "$r/etc/udev/rules.d/97-omen_encoder.rules.kencoder-disabled" ]]
[[ ! -e "$r/etc/udev/rules.d/97-omen_encoder.rules.kencoder-disabled.kencoder-disabled" ]]
sh "$r/postinst" configure >/dev/null 2>&1
mask_is_active "$r"
sh "$r/postrm" remove >/dev/null 2>&1
mask_is_active "$r"
sh "$r/postrm" purge >/dev/null 2>&1
[[ ! -L "$r/etc/udev/rules.d/97-omen_encoder.rules" ]]
[[ "$(cat "$r/etc/udev/rules.d/97-omen_encoder.rules")" == "$original" ]]
[[ ! -f "$r/var/lib/encoder/legacy-udev-migration" ]]
pass 'original /etc rule: backup, mask, idempotent, remove, purge restore'

prepare vendor-original; r=$REPLY
printf '%s\n' "$original" > "$r/usr/lib/udev/rules.d/97-omen_encoder.rules"
sh "$r/postinst" configure >/dev/null 2>&1
mask_is_active "$r"
[[ "$(cat "$r/usr/lib/udev/rules.d/97-omen_encoder.rules")" == "$original" ]]
sh "$r/postrm" purge >/dev/null 2>&1
[[ ! -L "$r/etc/udev/rules.d/97-omen_encoder.rules" ]]
[[ -f "$r/usr/lib/udev/rules.d/97-omen_encoder.rules" ]]
pass 'original vendor rule: mask without editing vendor files, remove on purge'

prepare modified-etc; r=$REPLY
printf '%s\n# local customization\n' "$original" > "$r/etc/udev/rules.d/97-omen_encoder.rules"
sh "$r/postinst" configure >/dev/null 2>&1
[[ ! -L "$r/etc/udev/rules.d/97-omen_encoder.rules" ]]
[[ ! -e "$r/etc/udev/rules.d/97-omen_encoder.rules.kencoder-disabled" ]]
pass 'modified original: do not touch'

prepare existing-mask; r=$REPLY
ln -s /dev/null "$r/etc/udev/rules.d/97-omen_encoder.rules"
sh "$r/postinst" configure >/dev/null 2>&1
sh "$r/postrm" purge >/dev/null 2>&1
mask_is_active "$r"
pass 'existing admin mask: preserved on install and purge'

prepare owned-etc; r=$REPLY
printf '%s\n' "$original" > "$r/etc/udev/rules.d/97-omen_encoder.rules"
KENCODER_TEST_OWNED=1 sh "$r/postinst" configure >/dev/null 2>&1
[[ ! -L "$r/etc/udev/rules.d/97-omen_encoder.rules" ]]
[[ ! -e "$r/etc/udev/rules.d/97-omen_encoder.rules.kencoder-disabled" ]]
pass 'dpkg-owned /etc rule: do not alter'

prepare backup-collision; r=$REPLY
printf '%s\n' "$original" > "$r/etc/udev/rules.d/97-omen_encoder.rules"
printf '%s\n' 'keep me' > "$r/etc/udev/rules.d/97-omen_encoder.rules.kencoder-disabled"
sh "$r/postinst" configure >/dev/null 2>&1
[[ ! -L "$r/etc/udev/rules.d/97-omen_encoder.rules" ]]
[[ $(cat "$r/etc/udev/rules.d/97-omen_encoder.rules.kencoder-disabled") == 'keep me' ]]
pass 'existing backup: no overwrite'

prepare interrupted-configure; r=$REPLY
mkdir -p "$r/var/lib/encoder"
printf '%s\n' etc > "$r/var/lib/encoder/legacy-udev-migration"
printf '%s\n' "$original" > "$r/etc/udev/rules.d/97-omen_encoder.rules.kencoder-disabled"
sh "$r/postinst" configure >/dev/null 2>&1
mask_is_active "$r"
sh "$r/postrm" purge >/dev/null 2>&1
[[ "$(cat "$r/etc/udev/rules.d/97-omen_encoder.rules")" == "$original" ]]
pass 'interrupted configuration: resume and restore on purge'

prepare no-old-rule; r=$REPLY
sh "$r/postinst" configure >/dev/null 2>&1
[[ ! -L "$r/etc/udev/rules.d/97-omen_encoder.rules" ]]
[[ ! -e "$r/var/lib/encoder/legacy-udev-migration" ]]
pass 'no legacy rule: no changes'

printf '%s\n' 'All legacy udev migration tests passed.'

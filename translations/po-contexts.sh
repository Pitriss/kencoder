#!/usr/bin/env bash
# Convert between gettext PO contexts and the Qt Linguist PO context format.
# Keep the source POT/PO files ordinary gettext catalogs; Qt needs "Context|".
set -euo pipefail
if [[ $# -ne 3 ]]; then
    printf 'Usage: bash %s to-qt|to-gettext INPUT OUTPUT\n' "$0" >&2
    exit 2
fi
mode=$1
input=$2
output=$3
if [[ "$mode" != to-qt && "$mode" != to-gettext ]]; then
    printf 'Invalid conversion: %s\n' "$mode" >&2
    exit 2
fi
[[ -r "$input" ]] || { printf 'Cannot read %s\n' "$input" >&2; exit 2; }
awk -v mode="$mode" '
    # The source PO files are gettext catalogs. In Qt PO files this header
    # activates the splitting of msgctxt "Context|disambiguation" into the
    # actual context and the optional disambiguation.
    /^"X-Qt-Contexts: (true|false)\\n"$/ { next }
    mode == "to-qt" && /^"Content-Transfer-Encoding: 8bit\\n"$/ {
        print
        print "\"X-Qt-Contexts: true\\n\""
        found_header++
        next
    }
    /^msgctxt "/ {
        if (mode == "to-qt" && $0 !~ /\|"$/)
            sub(/"$/, "|\"")
        else if (mode == "to-gettext")
            sub(/\|"$/, "\"")
    }
    { print }
    END {
        if (mode == "to-qt" && found_header != 1) {
            print "Missing or duplicate Content-Transfer-Encoding header" > "/dev/stderr"
            exit 2
        }
    }
' "$input" > "$output" || { rm -f "$output"; exit 2; }

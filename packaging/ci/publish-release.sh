#!/usr/bin/env bash
# Assemble exactly four independently versioned Debian packages into a GitHub Release.
# The non-rebuilt component is copied byte-for-byte from the previous release.
set -euo pipefail
shopt -s nullglob

: "${CI_TAG:?CI_TAG is required}"
: "${GH_REPO:?GH_REPO is required}"
: "${GH_TOKEN:?GH_TOKEN is required}"

fail() { echo "ERROR: $*" >&2; exit 1; }

if [[ "$CI_TAG" == encoder-v* ]]; then
  rebuilt=encoder
  new_tag_version=${CI_TAG#encoder-v}
elif [[ "$CI_TAG" == kencoder-v* ]]; then
  rebuilt=kencoder
  new_tag_version=${CI_TAG#kencoder-v}
elif [[ "$CI_TAG" == v* ]]; then
  rebuilt=both
  new_tag_version=${CI_TAG#v}
else
  fail "Unsupported tag: $CI_TAG"
fi
[[ "$new_tag_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]] || fail "Bad version in $CI_TAG"
expected_version=${new_tag_version/-/\~}
dpkg --compare-versions "$expected_version" ge 0 || fail "Bad Debian version: $expected_version"

[[ -d dist ]] || fail 'Missing dist directory'

# Always use the most recently PUBLISHED matching release, never CI artifacts
# or a draft release. This also supports migration from the original CI.
previous_tag=$(gh release list --limit 100 --json tagName,isDraft,publishedAt | jq -r '
  [.[] | select(.isDraft == false)
    | select(.tagName | test("^(v|encoder-v|kencoder-v)[0-9]+\\.[0-9]+\\.[0-9]+(-[a-zA-Z0-9.]+)?$"))]
  | sort_by(.publishedAt // "") | last | .tagName // empty
')

[[ "$previous_tag" != "$CI_TAG" ]] || fail 'Current tag already has a published release'
if [[ "$rebuilt" == both ]]; then
  [[ -z "$previous_tag" ]] || fail 'Combined vX.Y.Z tag is reserved for the first release; use component tags afterward'
else
  [[ -n "$previous_tag" ]] || fail 'First release must build both components with vX.Y.Z'
fi

previous_dir=''
if [[ -n "$previous_tag" ]]; then
  previous_dir=$(mktemp -d)
  trap 'rm -rf "$previous_dir"' EXIT
  gh release download "$previous_tag" --dir "$previous_dir" -p '*.deb' -p 'SHA256SUMS'
  [[ -s "$previous_dir/SHA256SUMS" ]] || fail "Previous release $previous_tag has no SHA256SUMS"
  (cd "$previous_dir" && sha256sum --check --status SHA256SUMS) || fail 'Previous release checksum verification failed'
fi

package_for() {
  local directory=$1 component=$2 arch=$3
  local matches=("$directory"/"$component"_*_"$arch".deb)
  [[ ${#matches[@]} -eq 1 && -f ${matches[0]:-} ]] || fail "Expected exactly one $component / $arch package in $directory"
  local file=${matches[0]}
  [[ $(dpkg-deb -f "$file" Package) == "$component" ]] || fail "Wrong package: $file"
  [[ $(dpkg-deb -f "$file" Architecture) == "$arch" ]] || fail "Wrong architecture: $file"
  local version
  version=$(dpkg-deb -f "$file" Version)
  [[ $(basename "$file") == "${component}_${version}_${arch}.deb" ]] || fail "Filename/metadata mismatch: $file"
  echo "$file"
}

version_for() {
  local directory=$1 component=$2
  local x y
  x=$(dpkg-deb -f "$(package_for "$directory" "$component" amd64)" Version)
  y=$(dpkg-deb -f "$(package_for "$directory" "$component" arm64)" Version)
  [[ "$x" == "$y" ]] || fail "Different $component versions across architectures"
  echo "$x"
}

if [[ -n "$previous_dir" ]]; then
  prev_encoder=$(version_for "$previous_dir" encoder)
  prev_kencoder=$(version_for "$previous_dir" kencoder)
  if [[ "$rebuilt" == encoder ]]; then
    for arch in amd64 arm64; do cp "$(package_for "$previous_dir" kencoder "$arch")" dist/; done
  else
    for arch in amd64 arm64; do cp "$(package_for "$previous_dir" encoder "$arch")" dist/; done
  fi
fi

# No gaps, duplicates, mixed architecture versions or unexpected packages.
files=(dist/*.deb)
[[ ${#files[@]} -eq 4 ]] || fail "Expected 4 .deb files, found ${#files[@]}"
current_encoder=$(version_for dist encoder)
current_kencoder=$(version_for dist kencoder)

if [[ "$rebuilt" == both ]]; then
  [[ "$current_encoder" == "$expected_version" && "$current_kencoder" == "$expected_version" ]] || fail 'Bootstrap versions do not match tag'
elif [[ "$rebuilt" == encoder ]]; then
  [[ "$current_encoder" == "$expected_version" && "$current_kencoder" == "$prev_kencoder" ]] || fail 'Incorrect reused GUI version'
  dpkg --compare-versions "$current_encoder" gt "$prev_encoder" || fail 'CLI version must increase'
else
  [[ "$current_kencoder" == "$expected_version" && "$current_encoder" == "$prev_encoder" ]] || fail 'Incorrect reused CLI version'
  dpkg --compare-versions "$current_kencoder" gt "$prev_kencoder" || fail 'GUI version must increase'
fi

# Read the actual Depends field of the .deb, not a source-file guess.
gui_deb=$(package_for dist kencoder amd64)
requires=$(dpkg-deb -f "$gui_deb" Depends | grep -oE '(^|, )encoder \(>= [^)]+\)' | sed -E 's/.*>= ([^)]+)\).*/\1/' || true)
[[ -n "$requires" ]] || fail 'GUI must declare a minimum encoder dependency'
dpkg --compare-versions "$current_encoder" ge "$requires" || fail "GUI requires encoder >= $requires, but release contains $current_encoder"
for arch in amd64 arm64; do
  gui_dep=$(dpkg-deb -f "$(package_for dist kencoder "$arch")" Depends)
  [[ "$gui_dep" == *"encoder (>= $requires)"* ]] || fail "Different GUI minimum encoder dependency on $arch"
done

(
  cd dist
  sha256sum ./*.deb > SHA256SUMS
)
printf 'encoder=%s\nkencoder=%s\nrebuilt=%s\nsource_previous_release=%s\n' \
  "$current_encoder" "$current_kencoder" "$rebuilt" "${previous_tag:-none}" > dist/VERSIONS.txt

notes=$(mktemp)
trap 'rm -f "$notes"; [[ -z "${previous_dir:-}" ]] || rm -rf "$previous_dir"' EXIT
cat > "$notes" <<EOF_NOTES
Debian 13 packages for amd64 and arm64.

- encoder CLI: $current_encoder
- kencoder GUI: $current_kencoder
- Built from this tag: $rebuilt
- Unchanged packages copied unchanged from: ${previous_tag:-not applicable (first release)}
- Minimum encoder version required by the GUI: $requires

See SHA256SUMS and VERSIONS.txt for reproducibility metadata.
EOF_NOTES

release_args=(--verify-tag --title "$CI_TAG" --notes-file "$notes")
if [[ "$new_tag_version" == *-* ]]; then release_args+=(--prerelease); fi
gh release create "$CI_TAG" dist/*.deb dist/SHA256SUMS dist/VERSIONS.txt "${release_args[@]}"
printf 'Published %s: encoder=%s, kencoder=%s, rebuilt=%s\n' "$CI_TAG" "$current_encoder" "$current_kencoder" "$rebuilt"

#!/usr/bin/env bash
# Independently build one binary Debian package from the tagged source tree.
# Run inside debian:trixie. All temporary build work stays outside the checkout.
set -euo pipefail

[[ $# -eq 3 ]] || { echo 'Usage: build-component.sh {encoder|kencoder} VERSION {amd64|arm64}' >&2; exit 2; }
component=$1
version=$2
arch=$3

[[ "$component" == encoder || "$component" == kencoder ]] || exit 2
[[ "$arch" == amd64 || "$arch" == arm64 ]] || exit 2
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]] || {
  echo "Unsupported release version: $version" >&2
  exit 2
}
# Git refnames cannot contain '~'. Debian sorts pre-releases before final releases.
deb_version=${version/-/\~}
dpkg --compare-versions "$deb_version" ge 0 || { echo "Invalid Debian version: $deb_version" >&2; exit 2; }

root=$(pwd -P)
[[ -f "$root/cli/encoder.c" && -f "$root/cli/Makefile" && -f "$root/gui/CMakeLists.txt" ]] || {
  echo 'Run in the KEncoder repository root' >&2; exit 2;
}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stage="$work/$component-$deb_version"
mkdir -p "$stage/debian"
cp "$root/LICENSE" "$stage/LICENSE"
cp "$root/debian/copyright" "$stage/debian/copyright"
mkdir -p "$stage/debian/source"
printf '3.0 (native)\n' > "$stage/debian/source/format"

if [[ "$component" == encoder ]]; then
  mkdir -p "$stage/cli" "$stage/packaging/udev" "$stage/docs/man/man1"
  cp "$root/cli/encoder.c" "$root/cli/keyboard_control.c" \
     "$root/cli/keyboard_control.h" "$root/cli/Makefile" "$stage/cli/"
  cp "$root/Makefile" "$stage/"
  cp "$root/packaging/udev/70-encoder-uaccess.rules" \
     "$root/packaging/udev/99-encoder.rules" "$stage/packaging/udev/"
  cp "$root/docs/man/man1/encoder.1" "$stage/docs/man/man1/"
  cp -a "$root/debian/encoder.udev" "$stage/debian/"
  cp "$root/debian/encoder.install" "$root/debian/encoder.manpages" "$stage/debian/"
  cp "$root/debian/encoder.postinst" "$root/debian/encoder.postrm" "$stage/debian/"
  cp "$root/packaging/ci/encoder/control" "$stage/debian/control"
  cp "$root/packaging/ci/encoder/rules" "$stage/debian/rules"
else
  cp -a "$root/gui" "$stage/gui"
  mkdir -p "$stage/packaging"
  cp "$root/packaging/kencoder.desktop" "$root/packaging/kencoder.png" "$stage/packaging/"
  min_encoder=$(tr -d '[:space:]' < "$root/packaging/ci/encoder-min-version")
  [[ "$min_encoder" =~ ^[0-9]+\.[0-9]+\.[0-9]+(\~[a-zA-Z0-9.]+)?$ ]] || { echo "Invalid minimum encoder version" >&2; exit 2; }
  dpkg --compare-versions "$min_encoder" ge 0
  sed "s/@MIN_ENCODER_VERSION@/$min_encoder/g" "$root/packaging/ci/kencoder/control.in" > "$stage/debian/control"
  cp "$root/packaging/ci/kencoder/rules" "$stage/debian/rules"
fi
chmod 755 "$stage/debian/rules"

printf '%s (%s) unstable; urgency=medium\n\n  * Tagged CI build of %s.\n\n -- Pitriss <5106460+Pitriss@users.noreply.github.com>  %s\n' \
  "$component" "$deb_version" "$component" "$(LC_ALL=C date -R)" > "$stage/debian/changelog"

(
  cd "$stage"
  dpkg-buildpackage -b -us -uc -j2
  if [[ "$component" == encoder ]]; then
    ./build/cli/encoder -h >/dev/null
  else
    # Isolate UI preferences from the release builder's desktop session.
    # Never allow a failed GUI self-test to publish a package silently.
    mkdir -p "$work/xdg-config" "$work/xdg-cache"
    if QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$work/xdg-config" \
       XDG_CACHE_HOME="$work/xdg-cache" ./build/gui/kencoder --self-test; then
      echo 'GUI self-test: PASS'
    else
      status=$?
      printf 'GUI self-test: FAIL (exit %s). Package NOT published.\n' "$status" >&2
      exit "$status"
    fi
  fi
)

package="$component"_"$deb_version"_"$arch".deb
file="$work/$package"
[[ -s "$file" ]] || { echo "Expected package missing: $file" >&2; exit 1; }
[[ $(dpkg-deb -f "$file" Package) == "$component" ]]
[[ $(dpkg-deb -f "$file" Version) == "$deb_version" ]]
[[ $(dpkg-deb -f "$file" Architecture) == "$arch" ]]

mkdir -p "$root/dist"
cp "$file" "$root/dist/$package"
echo "Built $root/dist/$package"

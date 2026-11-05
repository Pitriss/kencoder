#!/usr/bin/env bash
set -euo pipefail

: "${CI_COMPONENT:?CI_COMPONENT is required}"
: "${CI_ARCH:?CI_ARCH is required}"
: "${CI_TAG:?CI_TAG is required}"

case "$CI_COMPONENT" in encoder|kencoder) ;; *) echo "Invalid component: $CI_COMPONENT" >&2; exit 2 ;; esac
case "$CI_ARCH" in amd64|arm64) ;; *) echo "Invalid architecture: $CI_ARCH" >&2; exit 2 ;; esac

if [[ "$CI_TAG" == v* ]]; then
  version=${CI_TAG#v}
elif [[ "$CI_TAG" == "$CI_COMPONENT-v"* ]]; then
  version=${CI_TAG#"$CI_COMPONENT-v"}
else
  echo "Tag $CI_TAG cannot build $CI_COMPONENT" >&2
  exit 2
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential debhelper fakeroot dpkg-dev file
if [[ "$CI_COMPONENT" == kencoder ]]; then
  apt-get install -y --no-install-recommends cmake qt6-base-dev qt6-qpa-plugins qt6-declarative-dev extra-cmake-modules libkf6kcmutils-dev
fi
[[ $(dpkg --print-architecture) == "$CI_ARCH" ]] || { echo 'Wrong native architecture' >&2; exit 2; }

bash packaging/ci/build-component.sh "$CI_COMPONENT" "$version" "$CI_ARCH"

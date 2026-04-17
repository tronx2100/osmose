#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
DIST_DIR="${ROOT_DIR}/dist"
APP_BINARY="osmose-emulator"

if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "Error: dpkg-deb was not found. Install dpkg-dev." >&2
  exit 1
fi

mkdir -p "${DIST_DIR}"

"${ROOT_DIR}/compile.sh"

if [[ ! -x "${BUILD_DIR}/osmose" ]]; then
  echo "Error: expected binary not found at ${BUILD_DIR}/osmose" >&2
  exit 1
fi

MAJOR="$(awk '/^#define MAJOR/{print $3}' "${ROOT_DIR}/emulator/Version.h")"
MIDDLE="$(awk '/^#define MIDDLE/{print $3}' "${ROOT_DIR}/emulator/Version.h")"
MINOR="$(awk '/^#define MINOR/{print $3}' "${ROOT_DIR}/emulator/Version.h")"
VERSION="${MAJOR}.${MIDDLE}.${MINOR}"
ARCH="$(dpkg --print-architecture)"
PKG_NAME="osmose_${VERSION}_${ARCH}"
PKG_ROOT="${DIST_DIR}/${PKG_NAME}"

rm -rf "${PKG_ROOT}"
mkdir -p "${PKG_ROOT}/DEBIAN"
mkdir -p "${PKG_ROOT}/usr/games"
mkdir -p "${PKG_ROOT}/usr/share/applications"
mkdir -p "${PKG_ROOT}/usr/share/icons/hicolor/128x128/apps"
mkdir -p "${PKG_ROOT}/usr/share/doc/osmose"

install -m 0755 "${BUILD_DIR}/osmose" "${PKG_ROOT}/usr/games/${APP_BINARY}"
sed 's/^Exec=.*/Exec='"${APP_BINARY}"'/' "${ROOT_DIR}/osmose.desktop" > "${PKG_ROOT}/usr/share/applications/osmose.desktop"
chmod 0644 "${PKG_ROOT}/usr/share/applications/osmose.desktop"
install -m 0644 "${ROOT_DIR}/osmose.png" "${PKG_ROOT}/usr/share/icons/hicolor/128x128/apps/osmose.png"
install -m 0644 "${ROOT_DIR}/README" "${PKG_ROOT}/usr/share/doc/osmose/README"
install -m 0644 "${ROOT_DIR}/LICENSE" "${PKG_ROOT}/usr/share/doc/osmose/LICENSE"

cat > "${PKG_ROOT}/DEBIAN/control" <<CONTROL
Package: osmose
Version: ${VERSION}
Section: games
Priority: optional
Architecture: ${ARCH}
Depends: libc6, libasound2, libqt5core5a, libqt5gui5, libqt5opengl5, libqt5widgets5, zlib1g
Maintainer: Osmose Team <maintainer@example.org>
Description: Sega Master System / Game Gear emulator
 Qt-based Sega Master System / Game Gear emulator with OpenGL rendering and ALSA sound output.
CONTROL

dpkg-deb --root-owner-group --build "${PKG_ROOT}" "${DIST_DIR}/${PKG_NAME}.deb"

echo "Debian package created: ${DIST_DIR}/${PKG_NAME}.deb"

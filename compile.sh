#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

QMAKE_BIN=""
for CANDIDATE in qmake qmake-qt5 qmake6; do
  if command -v "${CANDIDATE}" >/dev/null 2>&1 && "${CANDIDATE}" -v >/dev/null 2>&1; then
    QMAKE_BIN="${CANDIDATE}"
    break
  fi
done

if [[ -z "${QMAKE_BIN}" ]]; then
  echo "Error: no working qmake binary found (tried qmake, qmake-qt5, qmake6)." >&2
  echo "Install Qt development tools, for example: qt5-qmake qtbase5-dev libqt5opengl5-dev" >&2
  exit 1
fi

if [[ "${CLEAN:-0}" == "1" ]]; then
  rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

"${QMAKE_BIN}" "${ROOT_DIR}/Osmose.pro"
make -j"$(nproc)"

echo "Build finished: ${BUILD_DIR}/osmose"

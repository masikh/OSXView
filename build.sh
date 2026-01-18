#!/bin/bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
PROFILE_OPTION="${OSXVIEW_PROFILE:-OFF}"

echo "=== OSXview bundler ==="

if ! command -v brew >/dev/null; then
    echo "Homebrew is required to install SDL dependencies." >&2
    exit 1
fi

if ! brew list sdl2 &> /dev/null; then
    echo "Installing SDL2..."
    brew install sdl2
fi

if ! brew list sdl2_ttf &> /dev/null; then
    echo "Installing SDL2_ttf..."
    brew install sdl2_ttf
fi

echo "Configuring CMake project (OSXVIEW_PROFILE=${PROFILE_OPTION})..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOSXVIEW_PROFILE="${PROFILE_OPTION}"

echo "Preparing bundle resources..."
cp "${PROJECT_ROOT}/Info.plist" "${BUILD_DIR}/Info.plist"
mkdir -p "${BUILD_DIR}/OSXView.app/Contents/Resources"
( cd "${BUILD_DIR}" && python3 "${PROJECT_ROOT}/create_icon_simple.py" )

echo "Building and creating OSXView.app..."
cmake --build "${BUILD_DIR}" --config Release

(cd "${BUILD_DIR}" && zip -r "${PROJECT_ROOT}/OSXView.app.zip" "OSXView.app")
echo "Bundle ready at: ${BUILD_DIR}/OSXView.app"
echo "Zipped bundle ready at: ${PROJECT_ROOT}/OSXView.app.zip"

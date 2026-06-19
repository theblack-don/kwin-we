#!/bin/bash
set -euo pipefail

# Build script for kwin-we
# Usage: ./build-kwin-we.sh [install-prefix] [extra-cmake-args...]
# Default install prefix is $HOME/.local
#
# This build uses a self-built KGlobalAccel/KGlobalAccelD installed under
# $HOME/.local (from /home/don/Projects/kglobalaccel and
# /home/don/Projects/kglobalacceld). CMAKE_PREFIX_PATH points there so the
# newer library is picked up instead of the system one.

cd "$(dirname "$0")"

BUILD_DIR="build"
INSTALL_PREFIX="${1:-$HOME/.local}"
shift || true

echo "Configuring kwin-we with install prefix: $INSTALL_PREFIX"
cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF \
    "$@"

echo "Building kwin-we..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo "Installing kwin-we to $INSTALL_PREFIX..."
cmake --install "$BUILD_DIR"

echo "Done. Binary installed to: $INSTALL_PREFIX/bin/kwin-we_wayland"

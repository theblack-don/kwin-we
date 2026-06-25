#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - A Kwin Window Environment
# Debian / Ubuntu Install Script
# =============================================================================
# This script installs all dependencies, builds kwin-we, installs noctalia-shell,
# and sets up session files so you can launch KineticWE from a TTY or a
# display greeter (SDDM, etc.).
#
# Tested on Debian 13 (Trixie) / Ubuntu 24.04+ with Plasma 6 backports enabled.
# For older releases, you may need to add a PPA or backports repo so that
# KF6, Qt6 and Plasma 6 packages are available.
#
# Usage:
#   ./install-debian.sh
#
# Environment variables:
#   INSTALL_PREFIX  - Where to install binaries (default: $HOME/.local)
#   NOCTALIA_URL    - Git URL for noctalia-shell (default: GitHub)
# =============================================================================

INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/.local}"
NOCTALIA_URL="${NOCTALIA_URL:-https://github.com/noctalia-dev/noctalia.git}"
KGLOBALACCELD_URL="${KGLOBALACCELD_URL:-https://invent.kde.org/plasma/kglobalacceld.git}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_JOBS="$(nproc)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }
step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------
check_debian() {
    if ! command -v apt-get >/dev/null 2>&1; then
        warn "This script is designed for Debian/Ubuntu (uses apt-get)."
        warn "Continuing anyway..."
    fi
}

check_sudo() {
    if ! sudo -n true 2>/dev/null; then
        info "You may be prompted for your sudo password to install system packages."
    fi
}

detect_install_sudo() {
    if [[ "$INSTALL_PREFIX" != "$HOME"* ]]; then
        echo "sudo"
    else
        echo ""
    fi
}

# ---------------------------------------------------------------------------
# Install system dependencies via apt
# ---------------------------------------------------------------------------
install_dependencies() {
    step "Installing system dependencies..."

    sudo apt-get update

    # Notes on package choices:
    #   - KF6 packages are kf6-<name>-dev on Debian Trixie / Ubuntu 24.04+.
    #     If your distro does not have them yet, enable backports (Debian) or
    #     the Kubuntu/backports PPA (Ubuntu).
    #   - Kirigami for KF6 ships as the "kirigami" package (no -dev split).
    #   - milou / aurorae / breeze are the runtime Plasma components; on
    #     Debian/Ubuntu they are NOT prefixed with "plasma-".
    #   - libplasma, plasma-activities and plasma-workspace ship both the
    #     runtime and the development headers in the same source package;
    #     the headers are in the -dev packages.
    #   - xcb-util is provided by libxcb-util0-dev, xcb-util-wm by
    #     libxcb-icccm4-dev, etc. (Debian splits the upstream xcb-util
    #     project into separate libraries).
    #   - libgbm and libEGL/libGLES ship from the mesa package family.
    #   - sdbus-cpp is not packaged in Debian/Ubuntu official repos; it is
    #     only used by a small portion of the codebase and the build will
    #     succeed without it. Install it from a PPA or build it from source
    #     if you need the full feature set.
    #   - We use --no-install-recommends to keep the install lean; if a
    #     component is missing at runtime, you can install the recommended
    #     packages manually.
    sudo apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git meson just pkg-config \
        qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-5compat-dev \
        qt6-wayland-dev qt6-tools-dev qt6-tools-dev-tools \
        extra-cmake-modules \
        kf6-kauth-dev kf6-kcolorscheme-dev kf6-kconfig-dev \
        kf6-kconfigwidgets-dev kf6-kcoreaddons-dev kf6-kcrash-dev \
        kf6-kdbusaddons-dev kf6-kglobalaccel-dev kf6-kguiaddons-dev \
        kf6-ki18n-dev kf6-kiconthemes-dev kf6-kidletime-dev \
        kf6-kio-dev kf6-kitemmodels-dev kf6-kitemviews-dev \
        kf6-kjobwidgets-dev kf6-knewstuff-dev kf6-knotifications-dev \
        kf6-kpackage-dev kf6-krunner-dev kf6-kservice-dev \
        kf6-ksvg-dev kf6-ktextwidgets-dev kf6-kwidgetsaddons-dev \
        kf6-kwindowsystem-dev kf6-kxmlgui-dev kf6-kcmutils-dev \
        kf6-kdeclarative-dev kf6-kglobalacceld \
        kirigami \
        kwayland-dev kdecoration-dev kscreenlocker-dev knighttime-dev \
        plasma-wayland-protocols \
        plasma-activities-dev libplasma-dev plasma-workspace-dev \
        kscreen milou aurorae breeze breeze-icon-theme breeze-cursor-theme \
        libepoxy-dev libvulkan-dev vulkan-headers \
        libwayland-dev wayland-protocols \
        libxkbcommon-dev libxkbcommon-x11-dev \
        libinput-dev libdrm-dev libgbm-dev \
        libdisplay-info-dev liblcms2-dev libxcvt-dev \
        libcanberra-dev \
        libx11-dev libxcb1-dev libxcb-keysyms1-dev libxcb-cursor-dev \
        libxcb-icccm4-dev libxcb-image0-dev libxcb-render-util0-dev \
        libxcb-shape0-dev libxcb-shm0-dev libxcb-sync0-dev libxcb-randr0-dev \
        libxcb-xfixes0-dev libxcb-xkb-dev libxcb-xinput-dev \
        xwayland \
        libsystemd-dev libpipewire-0.3-dev libevdev-dev \
        libqaccessibilityclient-dev hwdata \
        libegl1-mesa-dev libgles2-mesa-dev libfreetype-dev libfontconfig-dev \
        libcairo2-dev libpango1.0-dev libharfbuzz-dev libglib2.0-dev \
        libpam0g-dev libpolkit-gobject-1-dev libcurl4-openssl-dev \
        libwebp-dev librsvg2-dev libqalculate-dev libxml2-dev \
        libjemalloc-dev \
        xdg-desktop-portal xdg-desktop-portal-kde || {
            error "Failed to install some dependencies."
            error "On older Debian/Ubuntu releases you may need to enable"
            error "backports or a PPA so that KF6, Qt6 and Plasma 6 are available."
            exit 1
        }

    info "System dependencies installed."
}

# ---------------------------------------------------------------------------
# Clone, build & install kglobalacceld (in-process global shortcuts daemon)
# ---------------------------------------------------------------------------
build_kglobalacceld() {
    step "Building kglobalacceld (global shortcuts daemon)..."

    # kwin-we's GlobalShortcutsManager calls keyEvent(), pointerPressed(),
    # axisTriggered() and resetModifierOnlyState() directly on the
    # KGlobalAccelD object.  This API was introduced in kglobalacceld git
    # master (commit 099b1e9 "Dissolve KGlobalAccelInterface", June 2026)
    # and is NOT present in any released version — the latest stable tag
    # (v6.7.0) still routes those calls through a separate
    # KGlobalAccelInterface class.  The system package is therefore too
    # old to compile kwin-we against.
    #
    # We build kglobalacceld from git master into $INSTALL_PREFIX so that
    # kwin-we's CMake (which sets CMAKE_PREFIX_PATH=$INSTALL_PREFIX) finds
    # our newer build ahead of the system library.
    #
    # NOTE: kglobalacceld git master requires Qt >= 6.10.0 and KF6 >= 6.26.
    # Fedora 44+ and Arch rolling satisfy this; older distros may not.

    local kga_build_dir
    kga_build_dir="$(mktemp -d /tmp/kglobalacceld-build.XXXXXX)"

    cleanup_kga() {
        if [[ -n "${kga_build_dir:-}" && -d "$kga_build_dir" ]]; then
            rm -rf "$kga_build_dir"
        fi
    }
    trap cleanup_kga EXIT

    info "Cloning kglobalacceld into $kga_build_dir ..."
    git clone --depth 1 "$KGLOBALACCELD_URL" "$kga_build_dir"

    cd "$kga_build_dir"

    info "Configuring kglobalacceld..."
    cmake -B build -S . \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_TESTING=OFF

    info "Building kglobalacceld..."
    cmake --build build --parallel "$BUILD_JOBS"

    info "Installing kglobalacceld..."
    cmake --install build

    cd "$SCRIPT_DIR"

    info "kglobalacceld installed to $INSTALL_PREFIX"

    cleanup_kga
    trap - EXIT
}

# ---------------------------------------------------------------------------
# Build & install kwin-we
# ---------------------------------------------------------------------------
build_kwin_we() {
    step "Building kwin-we..."
    cd "$SCRIPT_DIR"

    if [[ ! -f "CMakeLists.txt" ]]; then
        error "Could not find kwin-we CMakeLists.txt in $SCRIPT_DIR"
        error "Please run this script from the root of the kwin-we repository."
        exit 1
    fi

    bash build-kwin-we.sh "$INSTALL_PREFIX" -DBUILD_TESTING=OFF
    info "kwin-we built and installed to $INSTALL_PREFIX"
}

# ---------------------------------------------------------------------------
# Clone, build & install noctalia-shell
# ---------------------------------------------------------------------------
build_noctalia() {
    step "Building noctalia-shell..."

    local noctalia_build_dir
    noctalia_build_dir="$(mktemp -d /tmp/noctalia-build.XXXXXX)"

    # Ensure temp dir is cleaned up on exit
    cleanup_noctalia() {
        if [[ -n "${noctalia_build_dir:-}" && -d "$noctalia_build_dir" ]]; then
            rm -rf "$noctalia_build_dir"
        fi
    }
    trap cleanup_noctalia EXIT

    info "Cloning noctalia-shell into $noctalia_build_dir ..."
    git clone --depth 1 "$NOCTALIA_URL" "$noctalia_build_dir"

    cd "$noctalia_build_dir"

    info "Configuring noctalia-shell..."
    just configure release "$INSTALL_PREFIX"

    info "Building noctalia-shell..."
    just build release

    info "Installing noctalia-shell..."
    local install_sudo
    install_sudo="$(detect_install_sudo)"
    $install_sudo just install release

    cd "$SCRIPT_DIR"

    info "noctalia-shell installed to $INSTALL_PREFIX"

    # Clean up the temp build dir and reset the trap now that we are done.
    cleanup_noctalia
    trap - EXIT
}

# ---------------------------------------------------------------------------
# Install portable startup script
# ---------------------------------------------------------------------------
install_startup_script() {
    step "Installing startup script..."

    mkdir -p "$INSTALL_PREFIX/bin"
    sed -e "s|@INSTALL_PREFIX@|$INSTALL_PREFIX|g" \
        "$SCRIPT_DIR/scripts/start-kineticwe.sh" \
        > "$INSTALL_PREFIX/bin/start-kineticwe"
    chmod +x "$INSTALL_PREFIX/bin/start-kineticwe"

    info "Installed $INSTALL_PREFIX/bin/start-kineticwe"
}

# ---------------------------------------------------------------------------
# Install desktop entry for greeters (SDDM, etc.)
# ---------------------------------------------------------------------------
install_desktop_file() {
    step "Installing desktop session entry..."

    # Display greeters like SDDM only look in /usr/share/wayland-sessions
    # (and /usr/share/xsessions), so the desktop file must live there
    # system-wide for the session to be selectable at login.
    local sessions_dir="/usr/share/wayland-sessions"
    sudo mkdir -p "$sessions_dir"

    local tmp_file
    tmp_file="$(mktemp)"
    sed -e "s|@INSTALL_PREFIX@|$INSTALL_PREFIX|g" \
        -e "s|@USER@|$USER|g" \
        "$SCRIPT_DIR/scripts/kineticwe.desktop.in" \
        > "$tmp_file"
    sudo install -m 0644 "$tmp_file" "$sessions_dir/kineticwe.desktop"
    rm -f "$tmp_file"

    info "Installed $sessions_dir/kineticwe.desktop"
}

# ---------------------------------------------------------------------------
# Optional: print PATH helper
# ---------------------------------------------------------------------------
ensure_path_hint() {
    if [[ ":$PATH:" != *":$INSTALL_PREFIX/bin:"* ]]; then
        warn "$INSTALL_PREFIX/bin is not in your PATH."
        warn "Add the following line to your shell profile (~/.bashrc or ~/.zshrc):"
        warn "  export PATH=\"$INSTALL_PREFIX/bin:\$PATH\""
        warn "  export LD_LIBRARY_PATH=\"$INSTALL_PREFIX/lib/x86_64-linux-gnu:\$LD_LIBRARY_PATH\""
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo "=========================================="
    echo "  KineticWE Debian / Ubuntu Installer"
    echo "  Install prefix: $INSTALL_PREFIX"
    echo "=========================================="
    echo

    check_debian
    check_sudo
    install_dependencies
    build_kglobalacceld
    build_kwin_we
    build_noctalia
    install_startup_script
    install_desktop_file
    ensure_path_hint

    echo
    echo "=========================================="
    echo "  Installation complete!"
    echo "=========================================="
    echo
    info "To start KineticWE from a TTY, run:"
    info "  $INSTALL_PREFIX/bin/start-kineticwe"
    echo
    info "To select KineticWE from SDDM or another greeter:"
    info "  Choose 'KineticWE' from the session menu at login."
    echo
    info "You may need to log out and back in for the session"
    info "entry to appear in your greeter."
}

main "$@"

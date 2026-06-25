#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - A Kwin Window Environment
# Fedora Install Script
# =============================================================================
# This script installs all dependencies, builds kwin-we, installs noctalia-shell,
# and sets up session files so you can launch KineticWE from a TTY or a
# display greeter (SDDM, etc.).
#
# Usage:
#   ./install-fedora.sh
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
check_fedora() {
    if [[ ! -f /etc/fedora-release ]]; then
        warn "This script is designed for Fedora. Continuing anyway..."
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
# Install system dependencies via dnf
# ---------------------------------------------------------------------------
install_dependencies() {
    step "Installing system dependencies..."

    # Some package names differ slightly between Fedora versions;
    # --skip-unavailable ignores packages that are not in the enabled
    # repositories or that are already installed, so re-running this
    # script is safe.
    sudo dnf install -y --skip-unavailable \
        cmake ninja-build gcc-c++ git meson just \
        qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel \
        qt6-qtsvg-devel qt6-qt5compat-devel qt6-qtwayland-devel \
        qt6-qttools-devel \
        extra-cmake-modules \
        kf6-kauth-devel kf6-kcolorscheme-devel kf6-kconfig-devel \
        kf6-kcoreaddons-devel kf6-kcrash-devel kf6-kdbusaddons-devel \
        kf6-kglobalaccel-devel kglobalacceld-devel \
        kf6-kio-devel kf6-kjobwidgets-devel \
        kf6-kguiaddons-devel kf6-ki18n-devel \
        kf6-kidletime-devel kf6-kpackage-devel kf6-kservice-devel \
        kf6-ksvg-devel kf6-kwidgetsaddons-devel kf6-kwindowsystem-devel \
        kf6-kdeclarative-devel kf6-kcmutils-devel kf6-knewstuff-devel \
        kf6-kxmlgui-devel kf6-krunner-devel kf6-knotifications-devel \
        kf6-kirigami \
        kwayland-devel kdecoration-devel kscreenlocker-devel \
        knighttime-devel plasma-wayland-protocols-devel \
        plasma-activities-devel libplasma-devel plasma-workspace-devel \
        plasma-milou aurorae plasma-breeze kscreen \
        libepoxy-devel vulkan-loader-devel vulkan-headers \
        wayland-devel wayland-protocols-devel \
        libxkbcommon-devel libxkbcommon-x11-devel \
        libinput-devel libdrm-devel mesa-libgbm-devel \
        libdisplay-info-devel lcms2-devel libxcvt-devel \
        libcanberra-devel \
        libX11-devel libxcb-devel xcb-util-keysyms-devel \
        xcb-util-cursor-devel xcb-util-devel xcb-util-wm-devel \
        xcb-util-image-devel xcb-util-renderutil-devel \
        xorg-x11-server-Xwayland \
        systemd-devel pipewire-devel libevdev-devel \
        qaccessibilityclient-qt6-devel pkgconf-pkg-config hwdata \
        libEGL-devel mesa-libGLES-devel freetype-devel fontconfig-devel \
        cairo-devel pango-devel harfbuzz-devel glib2-devel \
        sdbus-cpp-devel pam-devel polkit-devel libcurl-devel \
        libwebp-devel librsvg2-devel libqalculate-devel libxml2-devel \
        jemalloc-devel \
        xdg-desktop-portal xdg-desktop-portal-kde || {
            error "Failed to install some dependencies. If you are not on Fedora,"
            error "please install the equivalent packages for your distribution."
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
        warn "  export LD_LIBRARY_PATH=\"$INSTALL_PREFIX/lib64:\$LD_LIBRARY_PATH\""
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo "=========================================="
    echo "  KineticWE Fedora Installer"
    echo "  Install prefix: $INSTALL_PREFIX"
    echo "=========================================="
    echo

    check_fedora
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

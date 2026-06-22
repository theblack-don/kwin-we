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
        kf6-kglobalaccel-devel kf6-kglobalacceld-devel \
        kf6-kguiaddons-devel kf6-ki18n-devel \
        kf6-kidletime-devel kf6-kpackage-devel kf6-kservice-devel \
        kf6-ksvg-devel kf6-kwidgetsaddons-devel kf6-kwindowsystem-devel \
        kf6-kdeclarative-devel kf6-kcmutils-devel kf6-knewstuff-devel \
        kf6-kxmlgui-devel kf6-krunner-devel kf6-knotifications-devel \
        kf6-kirigami \
        kwayland-devel kdecoration-devel kscreenlocker-devel \
        knighttime-devel plasma-wayland-protocols-devel \
        plasma-activities-devel libplasma-devel plasma-milou aurorae plasma-breeze \
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

    local sessions_dir="$HOME/.local/share/wayland-sessions"
    mkdir -p "$sessions_dir"

    sed -e "s|@INSTALL_PREFIX@|$INSTALL_PREFIX|g" \
        -e "s|@USER@|$USER|g" \
        "$SCRIPT_DIR/scripts/kineticwe.desktop.in" \
        > "$sessions_dir/kineticwe.desktop"

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

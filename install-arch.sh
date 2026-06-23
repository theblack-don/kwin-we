#!/bin/bash
set -euo pipefail

# =============================================================================
# KineticWE - A Kwin Window Environment
# Arch Linux Install Script
# =============================================================================
# This script installs all dependencies, builds kwin-we, installs noctalia-shell,
# and sets up session files so you can launch KineticWE from a TTY or a
# display greeter (SDDM, etc.).
#
# Usage:
#   ./install-arch.sh
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
check_arch() {
    if [[ ! -f /etc/arch-release ]]; then
        warn "This script is designed for Arch Linux. Continuing anyway..."
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
# Install system dependencies via pacman
# ---------------------------------------------------------------------------
install_dependencies() {
    step "Installing system dependencies..."

    # --needed skips packages that are already up to date, so re-running this
    # script is safe.
    #
    # Notes on package choices:
    #   - Arch unifies runtime and headers into a single package (no -devel
    #     split), so kwayland, kdecoration, kscreenlocker, etc. include the
    #     headers needed to build against them.
    #   - mesa provides libGLES, libEGL and libgbm on Arch.
    #   - xcb-util is provided by the libxcb package family (e.g. xcb-util-wm).
    sudo pacman -S --needed --noconfirm \
        cmake ninja gcc git meson just pkgconf \
        qt6-base qt6-declarative qt6-svg qt6-5compat qt6-wayland qt6-tools \
        extra-cmake-modules \
        kauth kcolorscheme kconfig kcoreaddons kcrash kdbusaddons \
        kglobalaccel kglobalacceld kguiaddons ki18n kidletime kpackage \
        kservice ksvg kwidgetsaddons kwindowsystem kdeclarative kcmutils \
        knewstuff kxmlgui krunner knotifications kirigami \
        kwayland kdecoration kscreenlocker knighttime plasma-wayland-protocols \
        plasma-activities libplasma plasma-workspace milou aurorae breeze \
        libepoxy vulkan-headers vulkan-icd-loader libglvnd \
        wayland wayland-protocols \
        libxkbcommon libxkbcommon-x11 \
        libinput libdrm mesa \
        libdisplay-info lcms2 libxcvt \
        libcanberra \
        libx11 libxcb xcb-util-keysyms xcb-util-cursor xcb-util-wm \
        xcb-util-image xcb-util-renderutil \
        xorg-xwayland \
        systemd pipewire libevdev \
        libqaccessibilityclient-qt6 hwdata \
        freetype2 fontconfig \
        cairo pango harfbuzz glib2 \
        sdbus-cpp pam polkit curl libwebp librsvg libqalculate libxml2 \
        jemalloc \
        xdg-desktop-portal xdg-desktop-portal-kde || {
            error "Failed to install some dependencies. If you are not on Arch,"
            error "please use the install script that matches your distribution."
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
        warn "  export LD_LIBRARY_PATH=\"$INSTALL_PREFIX/lib:\$LD_LIBRARY_PATH\""
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo "=========================================="
    echo "  KineticWE Arch Linux Installer"
    echo "  Install prefix: $INSTALL_PREFIX"
    echo "=========================================="
    echo

    check_arch
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

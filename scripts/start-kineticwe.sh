#!/bin/bash
# =============================================================================
# KineticWE - A Kwin Window Environment
# Session startup script
# =============================================================================
# This script launches the KineticWE Wayland session. It can be run directly
# from a TTY after logging in, or invoked by a display manager / greeter.
#
# Environment:
#   INSTALL_PREFIX  - Override the installation prefix (default: $HOME/.local)
# =============================================================================

set -e

# Installation prefix for KineticWE binaries and libraries.
# When installed via install-fedora.sh this is set automatically.
# When run manually it defaults to $HOME/.local.
_INSTALL_PREFIX_="@INSTALL_PREFIX@"
if [[ "$_INSTALL_PREFIX_" == "@INSTALL_PREFIX@" ]]; then
    INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/.local}"
else
    INSTALL_PREFIX="${INSTALL_PREFIX:-$_INSTALL_PREFIX_}"
fi

# ---------------------------------------------------------------------------
# 1. Core paths for the custom KWin build
# ---------------------------------------------------------------------------
export PATH="$INSTALL_PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$INSTALL_PREFIX/lib64:$INSTALL_PREFIX/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# KWin needs this to find its own plugins (screencast.so, screenshot.so, etc.)
export QT_PLUGIN_PATH="$INSTALL_PREFIX/lib64/plugins"

# ---------------------------------------------------------------------------
# 2. Session / Desktop identity
# ---------------------------------------------------------------------------
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=KDE

# ---------------------------------------------------------------------------
# 3. Helper: locate xdg-desktop-portal executables
# ---------------------------------------------------------------------------
find_portal() {
    local name="$1"
    for path in \
        "/usr/libexec/$name" \
        "/usr/lib/$name" \
        "$INSTALL_PREFIX/libexec/$name"
    do
        if [[ -x "$path" ]]; then
            printf '%s\n' "$path"
            return 0
        fi
    done
    command -v "$name" 2>/dev/null || true
}

PORTAL_KDE="$(find_portal xdg-desktop-portal-kde)"
PORTAL="$(find_portal xdg-desktop-portal)"

# ---------------------------------------------------------------------------
# 4. Build the KWin startup payload (runs as KWin's child)
# ---------------------------------------------------------------------------
STARTUP_PAYLOAD_DIR="${XDG_RUNTIME_DIR:-/tmp}/kineticwe-$USER"
mkdir -p "$STARTUP_PAYLOAD_DIR"
STARTUP_PAYLOAD="$STARTUP_PAYLOAD_DIR/startup.sh"

# Write payload script
cat > "$STARTUP_PAYLOAD" << EOF
#!/bin/bash
# KineticWE startup payload — run by KWin as a detached child process.

export PATH="$INSTALL_PREFIX/bin:\$PATH"
export LD_LIBRARY_PATH="$INSTALL_PREFIX/lib64:$INSTALL_PREFIX/lib/x86_64-linux-gnu:\$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$INSTALL_PREFIX/lib64/plugins"
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=KDE

# ---------------------------------------------------------------------------
# 1. Start XDG Desktop Portals
# ---------------------------------------------------------------------------
# The KDE portal backend MUST start before the generic portal frontend so that
# the frontend can discover the backend's capabilities (Screenshot, ScreenCast).

killall -q xdg-desktop-portal-kde xdg-desktop-portal xdg-desktop-portal-gtk xdg-document-portal 2>/dev/null || true
sleep 1

mkdir -p "\$HOME/.local/share"

if [[ -x "$PORTAL_KDE" ]]; then
    nohup "$PORTAL_KDE" >"\$HOME/.local/share/xdg-desktop-portal-kde.log" 2>&1 &
else
    echo "Warning: xdg-desktop-portal-kde not found" >&2
fi

sleep 2

if [[ -x "$PORTAL" ]]; then
    nohup "$PORTAL" >"\$HOME/.local/share/xdg-desktop-portal.log" 2>&1 &
else
    echo "Warning: xdg-desktop-portal not found" >&2
fi

# ---------------------------------------------------------------------------
# 2. Start user applications
# ---------------------------------------------------------------------------
noctalia &
EOF

chmod +x "$STARTUP_PAYLOAD"

# ---------------------------------------------------------------------------
# 5. Stop any competing global-shortcuts daemon
# ---------------------------------------------------------------------------
# kwin-we embeds kglobalacceld and registers the org.kde.kglobalaccel D-Bus
# service at startup (KGlobalAccelD::init).  If a Plasma session was
# previously active, the standalone plasma-kglobalaccel.service (which runs
# /usr/libexec/kglobalacceld) may still own that D-Bus name.  When that
# happens kwin-we's init() fails to register the service, m_kglobalAccel is
# reset, and ALL keyboard shortcuts stop working with the error
# "error communicating with global shortcuts service".
#
# Stop any competing daemon so kwin-we can claim the name cleanly.
systemctl --user stop plasma-kglobalaccel.service 2>/dev/null || true
pkill -x kglobalacceld 2>/dev/null || true

# ---------------------------------------------------------------------------
# 6. Launch KWin
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# KWin will run the startup payload as a child process. The child inherits
# the environment captured here (including QT_PLUGIN_PATH).
exec kwin-we_wayland --xwayland "$STARTUP_PAYLOAD"

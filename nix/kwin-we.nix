{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  kdePackages,           # all KF6 / Plasma / KDE deps live here
  # Qt6
  qt6,
  # Other libraries
  libei,
  libinput,
  libevdev,
  libdrm,
  mesa,
  libdisplay-info,
  lcms2,
  libxcvt,
  libcanberra,
  pipewire,
  wayland,
  wayland-protocols,
  systemd,
  sdbus-cpp,
  xwayland,
  libX11,
  libxcb,
  xcbutilkeysyms,
  xcbutilcursor,
  xcbutilwm,
  xcbutilimage,
  xcbutilrenderutil,
  polkit,
  pam,
  jemalloc,
  version ? "git",
}:

let
  inherit (kdePackages)
    kauth kcolorscheme kconfig kcoreaddons kcrash kdbusaddons
    kglobalaccel kglobalacceld kguiaddons ki18n kidletime
    kcmutils knewstuff knotifications kpackage krunner
    kservice ksvg kwidgetsaddons kwindowsystem kdeclarative kxmlgui
    kirigami
    kdecoration kscreenlocker kwayland knighttime
    libplasma plasma-activities plasma-wayland-protocols
    aurorae breeze breeze-icons milou
    libqaccessibilityclient
    ;
in
stdenv.mkDerivation {
  pname = "kwin-we";
  inherit version;

  src = lib.cleanSource ../.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
    kdePackages.extra-cmake-modules
  ];

  buildInputs = [
    # Qt6
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtsvg
    qt6.qt5compat
    qt6.qtwayland
    qt6.qttools

    # KDE Frameworks 6 + Kirigami
    kauth
    kcolorscheme
    kconfig
    kcoreaddons
    kcrash
    kdbusaddons
    kglobalaccel
    kglobalacceld
    kguiaddons
    ki18n
    kidletime
    kcmutils
    knewstuff
    knotifications
    kpackage
    krunner
    kservice
    ksvg
    kwidgetsaddons
    kwindowsystem
    kdeclarative
    kxmlgui
    kirigami

    # Plasma / Wayland
    kdecoration
    kscreenlocker
    kwayland
    knighttime
    libplasma
    plasma-activities
    plasma-wayland-protocols
    aurorae
    breeze
    breeze-icons
    milou

    # Other
    libqaccessibilityclient
    libei
    libinput
    libevdev
    libdrm
    mesa
    libdisplay-info
    lcms2
    libxcvt
    libcanberra
    pipewire
    wayland
    wayland-protocols
    systemd
    sdbus-cpp
    polkit
    pam
    jemalloc

    # X11 / Xwayland
    libX11
    libxcb
    xcbutilkeysyms
    xcbutilcursor
    xcbutilwm
    xcbutilimage
    xcbutilrenderutil
    xwayland
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "-DBUILD_TESTING=OFF"
  ];

  # The compositor and many KF6/Plasma libs ship their QML modules
  # under <lib>/qt6/qml. Keep that discoverable by wrapQtAppsHook.
  qtQmlPrefix = "lib/qt6";

  meta = with lib; {
    description = "KineticWE — KWin window environment with native tiling";
    homepage = "https://github.com/anomalyco/kwin-we";
    license = licenses.gpl2Plus;
    platforms = platforms.linux;
    mainProgram = "kwin-we_wayland";
  };
}

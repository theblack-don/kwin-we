{
  lib,
  stdenv,
  cmake,
  ninja,
  just,
  meson,
  pkg-config,
  kdePackages,
  kwinWe,
  noctalia,
}:

stdenv.mkDerivation {
  pname = "kineticwe-devshell";
  version = "unstable";

  src = lib.cleanSource ../.;

  dontConfigure = true;
  dontBuild = true;
  dontInstall = true;

  nativeBuildInputs = [
    cmake
    ninja
    just
    meson
    pkg-config
    kdePackages.extra-cmake-modules
  ];

  # Build env: kwin-we + every KDE/Plasma dep it needs at build time,
  # plus the noctalia binary so `just configure && just build` works.
  buildInputs = kdePackages.kwin.buildInputs or [];

  shellHook = ''
    echo "KineticWE development shell"
    echo "  kwin-we source: $(pwd)"
    echo "  kwin-we:       ${kwinWe}/bin/kwin-we_wayland"
    echo "  noctalia:      ${noctalia}/bin/noctalia"
    echo
    echo "Build & install into a custom prefix:"
    echo "  just configure release \"\$HOME/.local\""
    echo "  just build release"
    echo "  just install release"
    echo
    echo "Or use the top-level install scripts:"
    echo "  ../install-fedora.sh"
    echo "  ../install-arch.sh"
    echo "  ../install-debian.sh"
    echo
    echo "Or build via the Nix flake:"
    echo "  nix build .#kwin-we"
    echo "  nix build .#session"
  '';
}

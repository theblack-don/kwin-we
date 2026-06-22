{
  description = "KineticWE — a KWin window environment (KWin fork) bundled with the noctalia shell";

  inputs = {
    # kwin-we requires Plasma 6.7 / KF6 6.27 development versions that
    # are only present on nixpkgs `master` today. nixos-unstable
    # (and release-26.05) currently track Plasma 6.6.5, which the
    # build refuses. Switch to `nixos-unstable` once nixpkgs catches up.
    nixpkgs.url = "github:NixOS/nixpkgs/master";

    flake-utils.url = "github:numtide/flake-utils";

    # Track the upstream noctalia-shell flake. Its outputs include the
    # binary, dev shell, home-manager and NixOS modules.
    noctalia = {
      url = "github:noctalia-dev/noctalia";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    { self
    , nixpkgs
    , flake-utils
    , noctalia
    }:
    {
      # ---- cross-system modules --------------------------------------------
      nixosModules.default = import ./nix/nixos-module.nix;
      homeModules.default = import ./nix/home-module.nix;

      overlays.default = final: prev: {
        kineticwe = self.packages.${prev.stdenv.hostPlatform.system}.default
          or (throw "kineticwe flake does not support ${prev.stdenv.hostPlatform.system}");
        kwin-we = self.packages.${prev.stdenv.hostPlatform.system}.kwin-we
          or (throw "kineticwe flake does not support ${prev.stdenv.hostPlatform.system}");
      };
    }
    //     flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          # KWin + noctalia build a lot of native code; keep the closure
          # to linux only.
          config.allowUnfree = true;
        };

        kwinWe = pkgs.callPackage ./nix/kwin-we.nix { };

        # The session package wires kwin-we + noctalia + xdg-desktop-portal
        # together and produces `bin/start-kineticwe`.
        session = pkgs.callPackage ./nix/session.nix {
          inherit kwinWe;
          noctalia = noctalia.packages.${system}.default;
        };
      in
      {
        # ---- packages ---------------------------------------------------------
        packages = {
          default = session;
          kwin-we = kwinWe;
          session = session;
        };

        # ---- apps ------------------------------------------------------------
        apps = {
          default = {
            type = "app";
            program = "${session}/bin/start-kineticwe";
            meta.description = "Start a KineticWE Wayland session (kwin-we + noctalia)";
          };
          kwin-we = {
            type = "app";
            program = "${kwinWe}/bin/kwin-we_wayland";
            meta.description = "Run the kwin-we Wayland compositor directly";
          };
        };

        # ---- dev shell -------------------------------------------------------
        devShells = {
          default = pkgs.callPackage ./nix/devshell.nix {
            inherit kwinWe;
            noctalia = noctalia.packages.${system}.default;
          };
        };
      }
    );
}

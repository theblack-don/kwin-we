# NixOS module for KineticWE.
#
# Usage (configuration.nix):
#   {
#     imports = [ inputs.kineticwe.nixosModules.default ];
#     programs.kineticwe.enable = true;
#   }
#
# This installs the session package system-wide and drops a
# wayland-sessions desktop entry so SDDM / greetd can offer
# "KineticWE" to all users.
#
{ config, lib, pkgs, ... }:

let
  cfg = config.programs.kineticwe;
in
{
  options.programs.kineticwe = {
    enable = lib.mkEnableOption "KineticWE — KWin window environment with noctalia";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.kineticwe or cfg.package;
      defaultText = lib.literalExpression "pkgs.kineticwe";
      description = ''
        The KineticWE session package providing `bin/start-kineticwe`.
        Typically the output of the kineticwe flake.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    # system-wide wayland-sessions entry.
    environment.etc."wayland-sessions/kineticwe.desktop".source =
      pkgs.runCommand "kineticwe.desktop" { } ''
        mkdir -p "$out"
        cat > "$out/kineticwe.desktop" <<EOF
        [Desktop Entry]
        Name=KineticWE
        Comment=A Kwin Window Environment
        Type=Application
        Exec=${cfg.package}/bin/start-kineticwe
        DesktopNames=KDE
        EOF
      '';
  };
}

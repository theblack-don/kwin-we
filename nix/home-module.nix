# Home Manager module for KineticWE.
#
# Usage:
#   programs.kineticwe = {
#     enable = true;
#     # package = pkgs.kineticwe; # optional override
#   };
#
# This will:
#   - Put `start-kineticwe` on the user's PATH
#   - Install a wayland-sessions desktop entry so SDDM / greetd can
#     list "KineticWE" in the session menu.
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

    autostart = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = ''
        Whether to autostart KineticWE from the graphical session.
        Most users should leave this `false` and pick "KineticWE" from
        the display manager instead.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    home.packages = [ cfg.package ];

    # wayland-sessions entry — picked up by SDDM, greetd, etc.
    xdg.dataFile."wayland-sessions/kineticwe.desktop".text = ''
      [Desktop Entry]
      Name=KineticWE
      Comment=A Kwin Window Environment
      Type=Application
      Exec=${cfg.package}/bin/start-kineticwe
      DesktopNames=KDE
    '';

    # Optional: autostart from within an existing DE (rare).
    xdg.autostart.kineticwe = lib.mkIf cfg.autostart {
      exec = "${cfg.package}/bin/start-kineticwe";
      comment = "Start KineticWE (kwin-we + noctalia)";
      terminal = false;
      type = "Application";
    };
  };
}

{
  description = "Lab6: BLE file transfer over GATT (BlueZ D-Bus + Qt6 Widgets)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              cmake
              ninja
              gnumake
              gcc
              pkg-config
              # Qt6 Widgets + QtDBus (QtDBus is part of qtbase on Linux).
              qt6.qtbase
              # BlueZ tools: bluetoothctl, btgatt-client, btmon.
              bluez
              # D-Bus tools: dbus-send, dbus-monitor.
              dbus
              # busctl (systemd) — удобно для интроспекции BlueZ.
              systemd
            ];
            shellHook = ''
              export CMAKE_PREFIX_PATH="${pkgs.qt6.qtbase}:$CMAKE_PREFIX_PATH"
              export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/lib/qt-6/plugins"
              echo "Lab6 BLE file transfer: cmake, g++, Qt6 (Widgets/DBus), BlueZ ready."
            '';
          };
        });
    };
}

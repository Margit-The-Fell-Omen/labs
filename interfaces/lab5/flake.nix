{
  description = "Lab5: USB HID probe GUI (libusb + sysfs + Qt6 Widgets) developer shell";

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
              gnumake
              gcc
              pkg-config
              libusb1
              # Qt6 Widgets (GUI). Даёт и заголовки, и библиотеки, и плагины.
              qt6.qtbase
            ];
            shellHook = ''
              # Чтобы CMake находил Qt6 (через ${pkgs.qt6.qtbase}).
              export CMAKE_PREFIX_PATH="${pkgs.qt6.qtbase}:$CMAKE_PREFIX_PATH"
              # Пути к плагинам Qt (xcb-платформа, стили) для запуска GUI из shell.
              export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/lib/qt-6/plugins"
              echo "Lab5 USB HID probe (Qt6 GUI): cmake, g++, pkg-config, libusb1, qtbase ready."
            '';
          };
        });
    };
}
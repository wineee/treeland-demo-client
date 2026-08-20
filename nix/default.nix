{
  stdenv,
  lib,
  nix-filter,
  cmake,
  ninja,
  pkg-config,
  wayland-scanner,
  sdl3,
  wayland,
  wayland-protocols,
  treeland-protocols,
}:

stdenv.mkDerivation rec {
  pname = "treeland-demo-client";
  version = "0.1.0";

  src = nix-filter.lib.filter {
    root = ./..;

    exclude = [
      ".git"
      "docs"
      (nix-filter.lib.matchExt "nix")
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wayland-scanner
  ];

  buildInputs = [
    sdl3
    wayland
    wayland-protocols
    treeland-protocols
  ];

  installPhase = ''
    mkdir -p $out/bin
    for exe in test-wine-window \
               treeland-demo-client \
               test-subsurface \
               test-xdg-dialog \
               test-cross-subsurface \
               test-cross-subsurface-child \
               test-pointer-constraints; do
      find build -name "$exe" -type f -executable | while read f; do
        cp -v "$f" "$out/bin/"
      done
    done
  '';

  meta = {
    description = "A collection of Wayland client demos for testing Treeland compositor protocols";
    homepage = "https://github.com/linuxdeepin/treeland-demo-client";
    license = with lib.licenses; [ gpl3Only ];
    platforms = lib.platforms.linux;
    maintainers = with lib.maintainers; [ ];
  };
}
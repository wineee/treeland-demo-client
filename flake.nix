{
  description = "A collection of Wayland client demos for testing Treeland compositor protocols";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    nix-filter.url = "github:numtide/nix-filter";
  };

  outputs = { self, nixpkgs, flake-utils, nix-filter }@inputs:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" "riscv64-linux" ]
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          treeland-protocols = pkgs.callPackage ./nix/treeland-protocols.nix { };

          packages = {
            treeland-demo-client = pkgs.callPackage ./nix {
              inherit nix-filter treeland-protocols;
            };
            treeland-protocols = treeland-protocols;
            default = self.packages.${system}.treeland-demo-client;
          };

          devShells.default = pkgs.mkShell {
            packages = with pkgs; [
              cmake
              ninja
              pkg-config
              wayland-scanner
              wayland
              wayland-protocols
              sdl3
            ];
          };
        in
        {
          inherit packages devShells;
        }
      );
}

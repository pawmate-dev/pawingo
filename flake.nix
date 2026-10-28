{
  description = "Pawingo development environment";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [ clang llvmPackages.clang-tools xmake just pkg-config ftxui sqlite expat zlib curl libarchive yaml-cpp prettier cargo rustc rustfmt clippy ];
          CC = "clang";
          CXX = "clang++";
        };
      });
    };
}

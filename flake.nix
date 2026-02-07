{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    self.submodules = true;
  };
  outputs = {
    self,
    nixpkgs,
    flake-utils,
    fenix,
  }:
    flake-utils.lib.eachDefaultSystem
    (
      system: let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [
            fenix.overlays.default
          ];
        };
        build = import ./sys/nix pkgs;
      in
        with pkgs; {
          formatter = pkgs.nixfmt-tree;
          devShells.default = mkShell.override {stdenv = stdenvNoCC;} {
            buildInputs =
              build.dependencies
              ++ [
                inetutils
              ];
            CC = "clang";
            CXX = "clang++";
          };
        }
    );
}

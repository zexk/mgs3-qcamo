{
  description = "MGS3 quick camouflage menu";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    imgui = { url = "github:ocornut/imgui/v1.92.9b"; flake = false; };
    minhook = { url = "github:TsudaKageyu/minhook/v1.3.4"; flake = false; };
  };
  outputs = { self, nixpkgs, imgui, minhook }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      mingw = pkgs.pkgsCross.mingwW64;
    in {
      packages.${system}.default = mingw.stdenv.mkDerivation {
        pname = "qcamo";
        version = "1.0.2";
        src = self;
        nativeBuildInputs = [ mingw.buildPackages.cmake mingw.buildPackages.ninja ];
        inherit imgui minhook;
        installPhase = ''
          mkdir -p $out
          cp qcamo.asi $out/
        '';
      };
    };
}

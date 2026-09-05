{
  description = "MGS3 quick camouflage menu";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    minhook = { url = "github:TsudaKageyu/minhook/v1.3.4"; flake = false; };
  };
  outputs = { self, nixpkgs, minhook }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      mingw = pkgs.pkgsCross.mingwW64;
    in {
      packages.${system}.default = mingw.stdenv.mkDerivation {
        pname = "qcamo";
        version = "0.0.1";
        src = self;
        nativeBuildInputs = [ mingw.buildPackages.cmake mingw.buildPackages.ninja ];
        inherit minhook;
        installPhase = ''
          mkdir -p $out
          cp qcamo.asi $out/
        '';
      };
    };
}

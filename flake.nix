{
  inputs.nixpkgs.url = "nixpkgs/nixpkgs-unstable";
  outputs = { nixpkgs, ... }:
    let
      inherit (nixpkgs) lib;
      inherit (lib) foldAttrs mergeAttrs mapAttrs;

      systems = lib.systems.flakeExposed;
      eachSystem = f: foldAttrs mergeAttrs { }
        (map (s: mapAttrs (_: v: { ${s} = v; }) (f s)) systems);
    in
    eachSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        packageDef = { stdenv, lib, fetchgit, cmake, ninja }:
          let
            inherit (builtins) fromJSON readFile;
            deps = mapAttrs (_: v: fetchgit (v // { fetchSubmodules = true; }))
              (fromJSON (readFile ./dependencies.json));
          in
          stdenv.mkDerivation {
            pname = "greengrass-lite";
            version = "0.0.1";
            src = lib.sourceFilesBySuffices ./.
              [ "cpp" "hpp" "h" "CMakeLists.txt" "cmake" "json" ];
            strictDeps = true;
            nativeBuildInputs = [ cmake ninja ];
            cmakeFlags = lib.mapAttrsToList
              (n: v: "-DFETCHCONTENT_SOURCE_DIR_${lib.toUpper n}=${v}")
              deps;
          };

        withDevShellPkgs = drv: drv.overrideAttrs (old: {
          CMAKE_EXPORT_COMPILE_COMMANDS = "1";
          nativeBuildInputs = old.nativeBuildInputs or [ ] ++ (with pkgs;
            [ clang-tools_16 gdb ]);
        });
      in
      rec {
        packages = {
          default = packages.clang;
          gcc = pkgs.callPackage packageDef { };
          clang = pkgs.callPackage packageDef { stdenv = pkgs.clangStdenv; };
        };

        devShells = mapAttrs (_: withDevShellPkgs) packages;

        formatter = pkgs.nixpkgs-fmt;
      });
}

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

        devTools = with pkgs; [
          coreutils
          clang-tools_16
          gdb
          cmake-format
          nixpkgs-fmt
          nodePackages.prettier
          fd
          git
        ];

        withDevShellPkgs = drv: drv.overrideAttrs (old: {
          CMAKE_EXPORT_COMPILE_COMMANDS = "1";
          nativeBuildInputs = old.nativeBuildInputs or [ ] ++ devTools;
        });

        runCheck = cmd: pkgs.runCommand "check" { }
          "cp --no-preserve=mode -r ${./.} src; cd src\n${cmd}\n touch $out";
      in
      rec {
        packages = {
          default = packages.clang;
          gcc = pkgs.callPackage packageDef { };
          clang = pkgs.callPackage packageDef { stdenv = pkgs.clangStdenv; };
        };

        devShells = mapAttrs (_: withDevShellPkgs) packages;

        checks = {
          formatting = runCheck ''
            ${lib.getExe formatter} .
            ${pkgs.diffutils}/bin/diff -rq --no-dereference . ${./.} | \
              sed -e 's/Files \(.*\) and .* differ/\1 not formatted/'
          '';
        };

        formatter =
          let
            format_cmd = pkgs.writeShellScript "format-file" ''
              PATH=${lib.makeBinPath devTools}
              case "$1" in
                *.c | *.h | *.cpp | *.hpp) clang-format -i "$1";;
                *.cmake | *CMakeLists.txt)
                   cmake-format -c ${./.cmake-format.yml} -i "$1";;
                *.nix) nixpkgs-fmt "$1";;
                *.md | *.json | *.yml) prettier --write "$1";;
              esac &>/dev/null
            '';
          in
          pkgs.writeShellScriptBin "format" ''
            for f in "$@"; do
              if [ -d "$f" ]; then
                (cd "$f"; ${pkgs.fd}/bin/fd -t f -H -x ${format_cmd})
              else
                ${format_cmd} "$f"
              fi
            done
          '';
      });
}

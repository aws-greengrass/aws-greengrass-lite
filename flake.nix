{
  inputs.flakelight.url = "github:nix-community/flakelight";
  outputs = { flakelight, ... }: flakelight ./. {
    packages = rec {
      default = { stdenv, lib, fetchgit, cmake, ninja }:
        let
          inherit (builtins) fromJSON readFile;
          deps = lib.mapAttrs (_: v: fetchgit (v // { fetchSubmodules = true; }))
            (fromJSON (readFile ./dependencies.json));
        in
        stdenv.mkDerivation {
          pname = "gglite";
          version = "0.0.1";
          src = lib.sourceFilesBySuffices ./.
            [ "cpp" "hpp" "c" "h" "CMakeLists.txt" "cmake" "json" ];
          strictDeps = true;
          nativeBuildInputs = [ cmake ninja ];
          cmakeBuildType = "Debug";
          cmakeFlags = lib.mapAttrsToList
            (n: v: "-DFETCHCONTENT_SOURCE_DIR_${lib.toUpper n}=${v}")
            deps;
        };

      gglite-clang = { callPackage, clangStdenv }:
        callPackage default { stdenv = clangStdenv; };
    };

    devShell = {
      inputsFrom = pkgs: [ pkgs.default ];
      packages = pkgs: with pkgs; [
        coreutils
        clang-tools_16
        gdb
        cppcheck
        cmake-format
        fd
        git
        git-secrets
        (python3.withPackages (ps: with ps; [ yapf python-lsp-server ]))
      ];
      env.CMAKE_EXPORT_COMPILE_COMMANDS = "1";
    };

    checks.cmake-lint = pkgs: ''
      ${pkgs.cmake-format}/bin/cmake-lint \
        $(${pkgs.fd}/bin/fd '.*\.cmake|CMakeLists.txt') \
        -c .cmake-format.yml --suppress-decorations
    '';

    formatters = {
      "*.c | *.h | *.cpp | *.hpp" = "clang-format -i";
      "*.cmake | *CMakeLists.txt" =
        "cmake-format -c ${./.cmake-format.yml} -i";
      "*.py" = "yapf -i";
    };
  };
}

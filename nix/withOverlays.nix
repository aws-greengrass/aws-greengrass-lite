final: prev:
let
  inherit (final) lib src fetchgit pkg-config clang-tools ggl-clang cmake;
  inherit (lib) fileset mapAttrs mapAttrsToList removePrefix toUpper;
  inherit (builtins) fromJSON readFile;

  deps = mapAttrs (_: v: fetchgit (v // { fetchSubmodules = true; }))
    (fromJSON (readFile (src + "/fc_deps.json")));
in
{
  gglUtil = rec {
    fetchContentFlags = mapAttrsToList
      (n: v: "-DFETCHCONTENT_SOURCE_DIR_${toUpper n}=${v}")
      deps;

    filteredSrc = lib.fileset.toSource {
      root = src;
      fileset = lib.fileset.fileFilter
        (file: lib.elem file.name [
          ".clang-tidy"
          "CMakeLists.txt"
          "fc_deps.json"
          "version"
          "run_nucleus"
          "recipe.yml"
          "greengrass-lite.target"
        ] ||
        lib.any file.hasExt [ "c" "h" "S" "sql" "cmake" "in" ])
        src;
    };

    llvmStdenv = final.overrideCC final.llvmPackages.stdenv
      (final.llvmPackages.stdenv.cc.override
        { inherit (final.llvmPackages) bintools; });

    clangBuildDir = llvmStdenv.mkDerivation {
      name = "clang-build-dir";
      nativeBuildInputs = [ pkg-config clang-tools ];
      inherit (ggl-clang) buildInputs;
      buildPhase = ''
        ${cmake}/bin/cmake -B $out -S ${filteredSrc} \
          -D CMAKE_BUILD_TYPE=Debug ${toString fetchContentFlags}
        rm $out/CMakeFiles/CMakeConfigureLog.yaml
      '';
      dontUnpack = true;
      dontPatch = true;
      dontConfigure = true;
      dontInstall = true;
      dontFixup = true;
    };

    cFiles = map (p: removePrefix ((toString src) + "/") (toString p))
      (fileset.toList (fileset.fileFilter (file: file.hasExt "c") src));
  };
}

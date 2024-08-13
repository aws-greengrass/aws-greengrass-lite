final: prev:
let
  inherit (final) lib src fetchgit;
  inherit (lib) mapAttrs mapAttrsToList toUpper;
  inherit (builtins) fromJSON readFile;

  deps = mapAttrs (_: v: fetchgit (v // { fetchSubmodules = true; }))
    (fromJSON (readFile (src + "/fc_deps.json")));
in
{
  gglUtil = {
    fetchContentFlags = mapAttrsToList
      (n: v: "-DFETCHCONTENT_SOURCE_DIR_${toUpper n}=${v}")
      deps;

    filteredSrc = lib.fileset.toSource {
      root = src;
      fileset = lib.fileset.fileFilter
        (file: lib.elem file.name [
          "CMakeLists.txt"
          ".clang-tidy"
          "fc_deps.json"
        ] ||
        lib.any file.hasExt [ "c" "h" "cmake" ])
        src;
    };

    llvmStdenv = final.overrideCC final.llvmPackages.stdenv
      (final.llvmPackages.stdenv.cc.override
        { inherit (final.llvmPackages) bintools; });
  };
}

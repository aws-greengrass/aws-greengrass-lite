{ stdenv
, lib
, fetchgit
, cmake
, ninja
, src
}:
let
  inherit (builtins) fromJSON readFile;
  inherit (lib) any elem fileset mapAttrs;
  deps = mapAttrs (_: v: fetchgit (v // { fetchSubmodules = true; }))
    (fromJSON (readFile (src + "/dependencies.json")));
in
stdenv.mkDerivation {
  pname = "gglite";
  version = "0.0.1";
  src = fileset.toSource {
    root = src;
    fileset = fileset.fileFilter
      (file: elem file.name [ "CMakeLists.txt" "version.script" ] ||
        any file.hasExt [ "cpp" "hpp" "c" "h" "cmake" "json" ])
      src;
  };
  strictDeps = true;
  nativeBuildInputs = [ cmake ninja ];
  hardeningDisable = [ "all" ];
  cmakeFlags = lib.mapAttrsToList
    (n: v: "-DFETCHCONTENT_SOURCE_DIR_${lib.toUpper n}=${v}")
    deps;
}

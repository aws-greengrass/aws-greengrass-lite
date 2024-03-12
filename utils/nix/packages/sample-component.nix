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
stdenv.mkDerivation (self: {
  pname = "sample-component";
  version = "0.0.1";
  src = fileset.toSource {
    root = src;
    fileset = fileset.unions [
      (src + "/dependencies.json")
      (src + "/utils/cmake/FetchContentFromDeps.cmake")
      (fileset.fileFilter
        (file: elem file.name [ "CMakeLists.txt" ] ||
          any file.hasExt [ "cpp" "hpp" "yaml" ])
        (src + "/samples/sample-component"))
    ];
  };
  preConfigure = "cd samples/sample-component";
  strictDeps = true;
  nativeBuildInputs = [ cmake ninja ];
  hardeningDisable = [ "all" ];
  cmakeFlags = lib.mapAttrsToList
    (n: v: "-DFETCHCONTENT_SOURCE_DIR_${lib.toUpper n}=${v}")
    deps;
})

{ stdenv
, lib
, fetchgit
, cmake
, ninja
, openssl
, src
, darwin ? null
}:
let
  inherit (builtins) fromJSON readFile;
  inherit (lib) fileset mapAttrs;
  deps = mapAttrs (_: v: fetchgit (v // { fetchSubmodules = true; }))
    (fromJSON (readFile (src + "/dependencies.json")));
  fetchcontentFlags = lib.mapAttrsToList
    (n: v: "-DFETCHCONTENT_SOURCE_DIR_${lib.toUpper n}=${v}")
    deps;
in
stdenv.mkDerivation {
  pname = "gglite";
  version = "0.0.1";
  src = fileset.toSource {
    root = src;
    fileset = fileset.unions [
      (src + "/CMakeLists.txt")
      (src + "/common.cmake")
      (src + "/dependencies.json")
      (src + "/nucleus")
      (src + "/plugin_api")
      (src + "/plugins")
      (src + "/utils/cmake")
    ];
  };
  strictDeps = true;
  nativeBuildInputs = [ cmake ninja ];
  buildInputs = [ openssl ] ++ lib.optional stdenv.isDarwin darwin.Security;
  hardeningDisable = [ "all" ];
  cmakeBuildType = if stdenv.isDarwin then "Debug" else "MinSizeRel";
  cmakeFlags = fetchcontentFlags ++ [
    "-DBUILD_TESTING=1"
  ];
  doCheck = true;
  checkPhase = ''
    runHook preCheck
    ctest -R nucleus --output-on-failure
    runHook postCheck
  '';
  passthru = { inherit fetchcontentFlags; };
}

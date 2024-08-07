{ src
, lib
, ggl-util
, pkg-config
, cmake
, clang-tools
, openssl
, curl
, argp-standalone
, sqlite
, libyaml
, ...
}:
let
  c-files = map (p: lib.removePrefix ((toString src) + "/") (toString p))
    (lib.fileset.toList (lib.fileset.fileFilter (file: file.hasExt "c") src));
in
ggl-util.llvmStdenv.mkDerivation {
  name = "check-clang-tidy";
  src = ggl-util.fixedSrc;
  nativeBuildInputs = [ pkg-config clang-tools ];
  buildInputs = [ openssl curl argp-standalone sqlite.dev libyaml.dev ];
  buildPhase = ''
    ${cmake}/bin/cmake -B build -D CMAKE_BUILD_TYPE=Debug
    clang-tidy -p build --warnings-as-errors='*' ${toString c-files}
  '';
  installPhase = "touch $out";
}

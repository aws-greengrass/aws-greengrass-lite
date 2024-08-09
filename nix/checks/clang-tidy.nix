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

  build-dir = ggl-util.llvmStdenv.mkDerivation {
    name = "clang-tidy-build-dir";
    src = ggl-util.fixedSrc;
    nativeBuildInputs = [ pkg-config clang-tools ];
    buildInputs = [ openssl curl argp-standalone sqlite.dev libyaml.dev ];
    buildPhase = ''
      ${cmake}/bin/cmake -B $out -D CMAKE_BUILD_TYPE=Debug
      rm $out/CMakeFiles/CMakeConfigureLog.yaml
    '';
    dontPatch = true;
    dontConfigure = true;
    dontInstall = true;
    dontFixup = true;
  };

  check-clang-tidy = file: ggl-util.llvmStdenv.mkDerivation {
    name = "clang-tidy-${file}";
    src = ggl-util.fixedSrc;
    nativeBuildInputs = [ pkg-config clang-tools ];
    buildInputs = [ openssl curl argp-standalone sqlite.dev libyaml.dev ];
    buildPhase = ''
      clang-tidy -p ${build-dir} --quiet --warnings-as-errors='*' ${file}
      touch $out
    '';
    dontPatch = true;
    dontConfigure = true;
    dontInstall = true;
    dontFixup = true;
  };
in
ggl-util.llvmStdenv.mkDerivation {
  name = "check-clang-tidy";
  buildInputs = map check-clang-tidy c-files;
  installPhase = "touch $out";
  dontUnpack = true;
  dontPatch = true;
  dontConfigure = true;
  dontBuild = true;
  dontFixup = true;
}

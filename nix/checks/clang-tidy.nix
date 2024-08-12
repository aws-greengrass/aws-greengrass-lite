{ src
, lib
, gglUtil
, ggl-clang
, pkg-config
, cmake
, clang-tools
, ...
}:
let
  c-files = map (p: lib.removePrefix ((toString src) + "/") (toString p))
    (lib.fileset.toList (lib.fileset.fileFilter (file: file.hasExt "c") src));

  build-dir = gglUtil.llvmStdenv.mkDerivation {
    name = "clang-tidy-build-dir";
    src = gglUtil.filteredSrc;
    nativeBuildInputs = [ pkg-config clang-tools ];
    inherit (ggl-clang) buildInputs;
    buildPhase = ''
      ${cmake}/bin/cmake -B $out -D CMAKE_BUILD_TYPE=Debug \
        ${toString gglUtil.fetchContentFlags}
      rm $out/CMakeFiles/CMakeConfigureLog.yaml
    '';
    dontPatch = true;
    dontConfigure = true;
    dontInstall = true;
    dontFixup = true;
  };

  check-clang-tidy = file: gglUtil.llvmStdenv.mkDerivation {
    name = "clang-tidy-${file}";
    src = gglUtil.filteredSrc;
    nativeBuildInputs = [ pkg-config clang-tools ];
    inherit (ggl-clang) buildInputs;
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
gglUtil.llvmStdenv.mkDerivation {
  name = "check-clang-tidy";
  buildInputs = map check-clang-tidy c-files;
  installPhase = "touch $out";
  dontUnpack = true;
  dontPatch = true;
  dontConfigure = true;
  dontBuild = true;
  dontFixup = true;
}

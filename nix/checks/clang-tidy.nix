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
  inherit (lib) escapeRegex fileset removePrefix;
  inherit (gglUtil) fetchContentFlags filteredSrc llvmStdenv;

  c-files = map (p: removePrefix ((toString src) + "/") (toString p))
    (fileset.toList (fileset.fileFilter (file: file.hasExt "c") src));

  build-dir = gglUtil.llvmStdenv.mkDerivation {
    name = "clang-tidy-build-dir";
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

  check-clang-tidy = file: llvmStdenv.mkDerivation {
    name = "clang-tidy-${file}";
    nativeBuildInputs = [ pkg-config clang-tools ];
    inherit (ggl-clang) buildInputs;
    buildPhase = ''
      clang-tidy -p ${build-dir} --quiet --warnings-as-errors='*' \
        --header-filter='^${escapeRegex (toString filteredSrc)}"}' \
        ${filteredSrc}/${file}
      touch $out
    '';
    dontUnpack = true;
    dontPatch = true;
    dontConfigure = true;
    dontInstall = true;
    dontFixup = true;
  };
in
llvmStdenv.mkDerivation {
  name = "check-clang-tidy";
  buildInputs = map check-clang-tidy c-files;
  installPhase = "touch $out";
  dontUnpack = true;
  dontPatch = true;
  dontConfigure = true;
  dontBuild = true;
  dontFixup = true;
}

{ lib
, gglUtil
, ggl-clang
, clang-tools
, ...
}:
let
  inherit (lib) escapeRegex;
  inherit (gglUtil) clangBuildDir filteredSrc llvmStdenv cFiles;

  check-clang-tidy = file: llvmStdenv.mkDerivation {
    name = "clang-tidy-${file}";
    nativeBuildInputs = [ clang-tools ];
    inherit (ggl-clang) buildInputs;
    buildPhase = ''
      clang-tidy -p ${clangBuildDir} --quiet --warnings-as-errors='*' \
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
  buildInputs = map check-clang-tidy cFiles;
  installPhase = "touch $out";
  dontUnpack = true;
  dontPatch = true;
  dontConfigure = true;
  dontBuild = true;
  dontFixup = true;
}

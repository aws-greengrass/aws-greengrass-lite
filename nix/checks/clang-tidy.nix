{ lib
, gglUtil
, ggl-clang
, clang-tools
, ...
}:
let
  inherit (lib) escapeRegex;
  inherit (gglUtil) llvmStdenv cFiles clangChecks;

  check-clang-tidy = file: llvmStdenv.mkDerivation {
    name = "clang-tidy-${file}";
    nativeBuildInputs = [ clang-tools ];
    inherit (ggl-clang) buildInputs;
    buildPhase = ''
      set -eo pipefail
      clang-tidy -p ${clangChecks.cmakeBuildDir} \
        --quiet --warnings-as-errors='*' \
        --header-filter='^${escapeRegex (toString clangChecks.src)}"}' \
        ${clangChecks.src}/${file} |\
        sed 's|${clangChecks.src}/||'
      touch $out
    '';
    dontUnpack = true;
    dontPatch = true;
    dontConfigure = true;
    dontInstall = true;
    dontFixup = true;
    allowSubstitutes = false;
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

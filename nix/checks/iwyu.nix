{ src
, gglUtil
, ggl-clang
, include-what-you-use
, ...
}:
let
  inherit (gglUtil) clangBuildDir filteredSrc llvmStdenv cFiles;

  check-iwyu = file: llvmStdenv.mkDerivation {
    name = "iwyu-${file}";
    nativeBuildInputs = [ include-what-you-use ];
    inherit (ggl-clang) buildInputs;
    buildPhase = ''
      set -eo pipefail
      white=$(printf "\e[1;37m")
      red=$(printf "\e[1;31m")
      clear=$(printf "\e[0m")
      iwyu_tool.py -p ${clangBuildDir} ${filteredSrc}/${file} -o clang -- \
        -Xiwyu --error -Xiwyu --check_also="${filteredSrc}/*" \
        -Xiwyu --mapping_file=${src}/misc/iwyu_mappings.yml |\
        { grep error: || true; } |\
        sed 's|\(.*\)error:\(.*\)|'$white'\1'$red'error:'$white'\2'$clear'|' |\
        sed 's|${filteredSrc}/||'
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
  name = "check-iwyu";
  buildInputs = map check-iwyu cFiles;
  installPhase = "touch $out";
  dontUnpack = true;
  dontPatch = true;
  dontConfigure = true;
  dontBuild = true;
  dontFixup = true;
}

{ src
, gglUtil
, ggl-clang
, include-what-you-use
, fd
, ...
}:
let
  inherit (gglUtil) llvmStdenv clangChecks;
in
llvmStdenv.mkDerivation {
  name = "check-iwyu";
  nativeBuildInputs = [ include-what-you-use fd ];
  inherit (ggl-clang) buildInputs;
  buildPhase = ''
    set -eo pipefail
    white=$(printf "\e[1;37m")
    red=$(printf "\e[1;31m")
    clear=$(printf "\e[0m")
    iwyu_tool.py -o clang -j $(nproc) -p ${clangChecks.cmakeBuildDir} \
      $(fd . ${clangChecks.src}/ -e c) -- \
      -Xiwyu --error -Xiwyu --check_also="${clangChecks.src}/*" \
      -Xiwyu --mapping_file=${src}/misc/iwyu_mappings.yml |\
      { grep error: || true; } |\
      sed 's|\(.*\)error:\(.*\)|'$white'\1'$red'error:'$white'\2'$clear'|' |\
      sed 's|${clangChecks.src}/||'
    touch $out
  '';
  dontUnpack = true;
  dontPatch = true;
  dontConfigure = true;
  dontInstall = true;
  dontFixup = true;
  allowSubstitutes = false;
}

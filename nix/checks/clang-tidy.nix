{ lib
, gglUtil
, ggl-clang
, clang-tools
, clangd-tidy
, fd
, ...
}:
let
  inherit (gglUtil) llvmStdenv clangChecks;

in
llvmStdenv.mkDerivation {
  name = "check-clangd-tidy";
  nativeBuildInputs = [ clang-tools clangd-tidy fd ];
  inherit (ggl-clang) buildInputs;
  buildPhase = ''
    set -eo pipefail
    clangd-tidy -j$(nproc) -p ${clangChecks.cmakeBuildDir} --color=always \
      $(fd . ${clangChecks.src} -e c -e h) |\
      sed 's|\.\.${clangChecks.src}/||'
    touch $out
  '';
  dontUnpack = true;
  dontPatch = true;
  dontConfigure = true;
  dontInstall = true;
  dontFixup = true;
  allowSubstitutes = false;
}

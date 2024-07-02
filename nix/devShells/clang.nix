{ lib, pkgs, ggl-util, moduleArgs, ... }: {
  imports = [ (moduleArgs.config.devShell pkgs) ];
  stdenv = lib.mkForce ggl-util.llvmStdenv;
}

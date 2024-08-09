{ lib, pkgs, gglUtil, moduleArgs, ... }: {
  imports = [ (moduleArgs.config.devShell pkgs) ];
  stdenv = lib.mkForce gglUtil.llvmStdenv;
}

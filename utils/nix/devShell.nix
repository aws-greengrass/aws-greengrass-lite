{ lib, pkgs, moduleArgs, ... }: _: {
  imports = [ (moduleArgs.config.devShells.ci pkgs) ];
  packages = with pkgs; ([
    (python3.withPackages (ps: with ps; [ yapf python-lsp-server ]))
    cmake-format
    gh
    gnutar
    temurin-jre-bin-17
    zig
  ] ++ (lib.optionals (!pkgs.stdenv.isDarwin) [
    gdb
  ]));
}

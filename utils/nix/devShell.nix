{ lib, pkgs, moduleArgs, ... }: {
  imports = [ (moduleArgs.config.devShells.ci pkgs) ];
  packages = with pkgs; ([
    (python3.withPackages (ps: with ps; [ yapf python-lsp-server ]))
    cmake-format
    gh
    gnutar
    temurin-jre-bin-17
    nodePackages.cspell
  ] ++ (lib.optionals (!pkgs.stdenv.isDarwin) [
    gdb
  ]));
}

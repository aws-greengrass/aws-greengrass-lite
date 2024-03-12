{ lib, pkgs, ... }: {
  inputsFrom = [ pkgs.default ];
  packages = with pkgs; ([
    coreutils
    clang-tools_16
    cppcheck
    cmake-format
    fd
    git
    git-secrets
    (python3.withPackages (ps: with ps; [ yapf python-lsp-server ]))
    temurin-jre-bin-17
    gnutar
  ] ++ (lib.optionals (!pkgs.stdenv.isDarwin) [
    gdb
  ]));
  env = {
    CMAKE_EXPORT_COMPILE_COMMANDS = "1";
    NIX_HARDENING_ENABLE = "";
    FETCHCONTENT_FLAGS = toString pkgs.default.fetchcontentFlags;
  };
}

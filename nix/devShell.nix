{
  inputsFrom = pkgs: [ pkgs.default ];
  packages = pkgs: with pkgs; [
    coreutils
    clang-tools_16
    gdb
    cppcheck
    cmake-format
    fd
    git
    git-secrets
    (python3.withPackages (ps: with ps; [ yapf python-lsp-server ]))
  ];
  env = {
    CMAKE_EXPORT_COMPILE_COMMANDS = "1";
    NIX_HARDENING_ENABLE = "";
  };
}

pkgs: {
  packages = with pkgs; [
    clang-tools
    clangd-tidy
    git
    gdb
  ];
  env.NIX_HARDENING_ENABLE = "";
  shellHook = ''
    export MAKEFLAGS=-j
  '';
}

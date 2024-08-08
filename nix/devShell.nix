pkgs: {
  packages = with pkgs; [
    clang-tools
    git
    git-secrets
  ];
  env.NIX_HARDENING_ENABLE = "";
}

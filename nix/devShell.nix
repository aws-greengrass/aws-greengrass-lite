pkgs: {
  shellHook = "export AR=gcc-ar";
  packages = with pkgs; [
    pkg-config
    clang-tools
    bear
    openssl
    git
    git-secrets
  ];
}

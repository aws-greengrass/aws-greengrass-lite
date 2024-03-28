{ pkgs, ... }: {
  packages = with pkgs; [
    clang-tools_17
    coreutils
    cppcheck
    git
    git-secrets
    openssl
  ];
  env = {
    NIX_HARDENING_ENABLE = "";
    FETCHCONTENT_FLAGS = toString pkgs.dependencies.fetchcontentFlags;
  };
}

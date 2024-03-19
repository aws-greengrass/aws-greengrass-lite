{ pkgs, ... }: {
  inputsFrom = [ pkgs.gglite-clang ];
  packages = with pkgs; [
    clang-tools_16
    coreutils
    cppcheck
    git
    git-secrets
  ];
  env = {
    NIX_HARDENING_ENABLE = "";
    FETCHCONTENT_FLAGS = toString pkgs.default.fetchcontentFlags;
  };
}

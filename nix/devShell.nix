pkgs:
let
  poetry2nix = pkgs.inputs.poetry2nix.lib.mkPoetry2Nix { inherit pkgs; };
in
{
  packages = (with pkgs; [
    clang-tools
    clangd-tidy
    git
    git-secrets
    gdb
    poetry
  ]) ++ [
    (poetry2nix.mkPoetryEnv {
      projectDir = pkgs.src + "/misc";
      preferWheels = true;
    })
  ];
  env.NIX_HARDENING_ENABLE = "";
  shellHook = ''
    export MAKEFLAGS=-j$(nproc)
  '';
}

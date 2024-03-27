{ runCommand, lib, src, python3, git, cacert, diffutils, ... }:
runCommand "check-deps-hashes"
{
  strictDeps = true;
  nativeBuildInputs = [ python3 git cacert diffutils ];

  impureEnvVars = lib.fetchers.proxyImpureEnvVars;
  outputHash = builtins.hashFile "sha256" (src + "/dependencies.json");
  outputHashMode = "flat";
  outputHashAlgo = "sha256";
} ''
  cp --no-preserve=mode ${src}/dependencies.json .
  python ${src}/utils/scripts/update_hashes.py
  diff ${src}/dependencies.json dependencies.json -u --color=always
  cp dependencies.json $out
''

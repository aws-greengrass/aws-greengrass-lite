{ lib
, fetchgit
, src
, runCommand
}:
let
  inherit (builtins) fromJSON readFile;
  inherit (lib) mapAttrs mapAttrsToList;

  deps = mapAttrs (_: v: fetchgit (v // { fetchSubmodules = true; }))
    (fromJSON (readFile (src + "/dependencies.json")));
  fetchcontentFlags = lib.mapAttrsToList
    (n: v: "-DFETCHCONTENT_SOURCE_DIR_${lib.toUpper n}=${v}")
    deps;
in
runCommand "dependencies"
{ passthru = { inherit fetchcontentFlags deps; }; }
  (''
    mkdir $out
    cd $out
  '' +
  (toString (mapAttrsToList
    (k: v: ''
      ln -s ${v} ${k}
    '')
    deps)))

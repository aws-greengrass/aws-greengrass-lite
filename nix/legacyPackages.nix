pkgs: {
  _type = "pkgs";
  cached-paths = pkgs.stdenv.mkDerivation {
    name = "cached-paths";
    exportReferencesGraph =
      let
        getAttrSet = name: pkgs.lib.mapAttrs'
          (n: pkgs.lib.nameValuePair ("${name}-" + n))
          pkgs.outputs'.${name};
        cached-outputs = pkgs.linkFarm "cached-outputs" (
          (getAttrSet "packages") //
          (getAttrSet "devShells") //
          (getAttrSet "checks") //
          { "formatter" = pkgs.outputs'.formatter; }
        );
      in
      [ "cache-paths" cached-outputs.drvPath ];
    buildPhase =
      "grep '^/nix/store/' < cache-paths | sort | uniq > $out";
    dontUnpack = true;
    dontPatch = true;
    dontConfigure = true;
    dontInstall = true;
    dontFixup = true;
    allowSubstitutes = false;
  };
}

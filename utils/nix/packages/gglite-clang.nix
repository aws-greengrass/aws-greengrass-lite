{ default
, llvmPackages_17
, overrideCC
}:
let
  llvmStdenvFor = llvmPackages:
    overrideCC llvmPackages.stdenv (llvmPackages.stdenv.cc.override {
      inherit (llvmPackages) bintools;
    });
  stdenv = llvmStdenvFor llvmPackages_17;
in
default.override { inherit stdenv; }

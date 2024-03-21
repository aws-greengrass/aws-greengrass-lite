{ default
, llvmPackages_15
, overrideCC
}:
let
  llvmStdenvFor = llvmPackages:
    overrideCC llvmPackages.stdenv (llvmPackages.stdenv.cc.override {
      inherit (llvmPackages) bintools;
    });
  stdenv = llvmStdenvFor llvmPackages_15;
in
default.override { inherit stdenv; }

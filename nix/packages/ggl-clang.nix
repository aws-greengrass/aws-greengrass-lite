{ gglUtil
, default
}:
default.override {
  stdenv = gglUtil.llvmStdenv;
}

{ ggl-util
, default
}:
default.override {
  stdenv = ggl-util.llvmStdenv;
}

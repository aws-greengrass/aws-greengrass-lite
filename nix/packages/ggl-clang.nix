{ ggl-util
, default
}:
(default.overrideAttrs {
  preConfigure = "";
}).override {
  stdenv = ggl-util.llvmStdenv;
}

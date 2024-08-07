{ ggl-util
, stdenv
, pkg-config
, cmake
, ninja
, openssl
, curl
, argp-standalone
, sqlite
, libyaml
, defaultMeta
}:
stdenv.mkDerivation {
  name = "aws-greengrass-lite";
  src = ggl-util.fixedSrc;
  nativeBuildInputs = [ pkg-config cmake ninja ];
  buildInputs = [ openssl curl argp-standalone sqlite.dev libyaml.dev ];
  cmakeBuildType = "MinSizeRel";
  cmakeFlags = [ "-DENABLE_WERROR=1" ];
  meta = defaultMeta;
}

{ ggl-util
, stdenv
, pkg-config
, cmake
, ninja
, openssl
, argp-standalone
, sqlite
, defaultMeta
}:
stdenv.mkDerivation {
  name = "aws-greengrass-lite";
  src = ggl-util.fixedSrc;
  nativeBuildInputs = [ pkg-config cmake ninja ];
  buildInputs = [ openssl argp-standalone sqlite.dev ];
  meta = defaultMeta;
}

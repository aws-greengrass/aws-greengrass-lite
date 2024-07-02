{ ggl-util
, stdenv
, pkg-config
, openssl
, argp-standalone
, sqlite
, defaultMeta
}:
stdenv.mkDerivation {
  name = "ggl";
  src = ggl-util.fixedSrc;
  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ openssl argp-standalone sqlite.dev ];
  preConfigure = "export AR=gcc-ar";
  installFlags = [ "DESTDIR=$(out)" ];
  meta = defaultMeta;
}

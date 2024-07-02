{ ggl-util
, stdenv
, pkg-config
, openssl
, argp-standalone
, defaultMeta
}:
stdenv.mkDerivation {
  name = "ggl";
  src = ggl-util.fixedSrc;
  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ openssl argp-standalone ];
  preConfigure = "export AR=gcc-ar";
  installFlags = [ "DESTDIR=$(out)" ];
  meta = defaultMeta;
}

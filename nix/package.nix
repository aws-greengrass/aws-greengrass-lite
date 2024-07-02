{ ggl-util
, stdenv
, pkg-config
, openssl
, defaultMeta
}:
stdenv.mkDerivation {
  name = "ggl";
  src = ggl-util.fixedSrc;
  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ openssl ];
  preConfigure = "export AR=gcc-ar";
  installFlags = [ "DESTDIR=$(out)" ];
  meta = defaultMeta;
}

{ gglUtil
, stdenv
, pkg-config
, cmake
, ninja
, openssl
, curl
, libuuid
, argp-standalone
, sqlite
, libyaml
, systemd
, defaultMeta
}:
stdenv.mkDerivation {
  name = "aws-greengrass-lite";
  src = gglUtil.filteredSrc;
  nativeBuildInputs = [ pkg-config cmake ninja ];
  buildInputs = [ openssl curl libuuid argp-standalone sqlite.dev libyaml.dev systemd ];
  cmakeBuildType = "MinSizeRel";
  cmakeFlags = gglUtil.fetchContentFlags ++ [ "-DENABLE_WERROR=1" ];
  meta = defaultMeta;
}

{ lib
, gglUtil
, src
, stdenv
, pkg-config
, cmake
, ninja
, openssl
, curl
, uriparser
, libuuid
, libevent
, argp-standalone
, sqlite
, libyaml
, libzip
, dbus
, systemdLibs
, defaultMeta
}:
stdenv.mkDerivation {
  name = "aws-greengrass-lite";
  src = lib.fileset.toSource { root = src; fileset = gglUtil.buildFileset; };
  nativeBuildInputs = [ pkg-config cmake ninja ];
  buildInputs = [
    openssl
    curl
    uriparser
    libuuid
    sqlite.dev
    libevent
    libyaml.dev
    dbus
    systemdLibs
    libzip
  ] ++ lib.optional (!stdenv.hostPlatform.isGnu) argp-standalone;
  cmakeBuildType = "MinSizeRel";
  cmakeFlags = gglUtil.fetchContentFlags ++ [ "-DENABLE_WERROR=1" ];
  meta = defaultMeta;
}

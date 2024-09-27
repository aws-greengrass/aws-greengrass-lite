{ lib
, gglUtil
, stdenv
, pkg-config
, cmake
, ninja
, openssl
, curl
, libuuid
, libevent
, argp-standalone
, sqlite
, libyaml
, dbus
, systemdLibs
, defaultMeta
}:
stdenv.mkDerivation {
  name = "aws-greengrass-lite";
  src = gglUtil.filteredSrc;
  nativeBuildInputs = [ pkg-config cmake ninja ];
  buildInputs = [
    openssl
    curl
    libuuid
    sqlite.dev
    libevent
    libyaml.dev
    dbus
    systemdLibs
  ] ++ lib.optional (!stdenv.hostPlatform.isGnu) argp-standalone;
  cmakeBuildType = "MinSizeRel";
  cmakeFlags = gglUtil.fetchContentFlags ++ [ "-DENABLE_WERROR=1" ];
  meta = defaultMeta;
}

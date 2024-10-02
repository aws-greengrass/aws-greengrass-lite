# cspell:ignore muslpi
{ pkgsCross
, moduleArgs
}:
pkgsCross.muslpi.${moduleArgs.config.pname}

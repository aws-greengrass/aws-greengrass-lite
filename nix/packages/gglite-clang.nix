{ clangStdenv
, default
}:
default.override { stdenv = clangStdenv; }

{ ggl-util, pkg-config, bear, clang-tools, openssl, ... }:
ggl-util.llvmStdenv.mkDerivation {
  name = "check-clang-tidy";
  src = ggl-util.fixedSrc;
  nativeBuildInputs = [ pkg-config bear clang-tools ];
  buildInputs = [ openssl ];
  buildPhase = ''
    make PROFILE=debug compile_commands.json
    clang-tidy $(find . -name "*.c" -not -path "./deps/*") \
      --warnings-as-errors='*'
  '';
  installPhase = "touch $out";
}

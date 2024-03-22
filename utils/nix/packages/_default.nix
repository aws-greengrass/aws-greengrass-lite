{ stdenv
, lib
, dependencies
, cmake
, ninja
, openssl
, src
, runCommand
, gnutar
, darwin ? null
}:
let
  inherit (lib) any concatStrings fileset;

  # All files needed for build
  baseFileset = fileset.unions (map (f: src + f) [
    "/CMakeLists.txt"
    "/common.cmake"
    "/dependencies.json"
    "/nucleus"
    "/plugin_api"
    "/plugins"
    "/utils/cmake"
  ]);

  # Files used by cmake to configure
  cmakeFileset = fileset.intersection
    baseFileset
    (fileset.fileFilter
      (file: any file.hasExt [ "txt" "cmake" "json" "yaml" "yml" ])
      src);

  # Files checked by cmake but not used
  cmakeExtraFiles = map (lib.path.removePrefix src)
    (fileset.toList (fileset.difference baseFileset cmakeFileset));

  # Source with cmake-used files, and with empty cmake-checked files
  # Empty source files are needed since cmake fails if sources are not found
  # Using empty files prevents rebuilds when the actual source file changes
  cmakeSrc = runCommand "source" { } (''
    cp -r ${fileset.toSource { root = src; fileset = cmakeFileset; }} $out
    chmod -R +w $out
    cd $out
  '' + (concatStrings (map
    (path: ''
      mkdir -p "$(dirname "${path}")"
      touch "${path}"
    '')
    cmakeExtraFiles)));

  commonArgs = {
    version = "0.0.1";
    strictDeps = true;
    nativeBuildInputs = [ cmake ninja ];
    buildInputs = [ openssl ];
    hardeningDisable = [ "all" ];
    cmakeBuildType = "MinSizeRel";
    cmakeDir = "/build/source";
    cmakeBuildDir = "/tmp/build";
    cmakeFlags = dependencies.fetchcontentFlags ++ [
      "-GNinja"
      "-DBUILD_TESTING=1"
      "-DCMAKE_INSTALL_PREFIX=/tmp/install"
    ];
    dontAddPrefix = true;
  } // lib.optionalAttrs stdenv.isDarwin {
    buildInputs = [ openssl darwin.Security ];
    cmakeBuildType = "Debug";
    postUnpack = ''
      rm -rf /tmp/source
      cp -r source /tmp
      cmakeDir=/tmp/source
    '';
  };

  # This intermediate drv is not reproducible :(
  buildDir = stdenv.mkDerivation (commonArgs // {
    pname = "gglite-build";
    src = cmakeSrc;
    # cache dependencies
    ninjaFlags = [
      "aws-crt-cpp"
      "IotIdentity-cpp"
      "aws-c-http"
      "aws-c-event-stream"
      "yaml-cpp"
      "Catch2WithMain"
    ];
    installPhase = ''
      ${gnutar}/bin/tar -czf $out --posix --sort=name \
        --numeric-owner --owner=0 --group=0 -- .
    '';
    dontFixup = true;
  });
in
stdenv.mkDerivation (commonArgs // {
  pname = "gglite";
  src = fileset.toSource { root = src; fileset = baseFileset; };
  configurePhase = ''
    mkdir -p /tmp/build
    cd /tmp/build
    ${gnutar}/bin/tar -zxf ${buildDir}
  '';
  doCheck = true;
  checkPhase = ''
    runHook preCheck
    ctest -R nucleus --output-on-failure
    runHook postCheck
  '';
  postInstall = ''
    mv /tmp/install $out
  '';
})

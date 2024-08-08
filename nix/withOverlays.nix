final: prev: {
  ggl-util = {
    fixedSrc =
      let
        inherit (final) lib src fetchgit runCommand;

        filtered-src = lib.fileset.toSource {
          root = src;
          fileset = lib.fileset.fileFilter
            (file: lib.elem file.name [ "CMakeLists.txt" ".clang-tidy" ] ||
              lib.any file.hasExt [ "c" "h" "cmake" ])
            src;
        };

        core_mqtt = fetchgit {
          url = "https://github.com/FreeRTOS/coreMQTT.git";
          rev = "58d626a25839ed20b19a57fdff34a41f36dacd05";
          hash = "sha256-QJyK0LGPCFMx6Q1/VyNBS8m5ywAiQcbgLdPRkA3+ilw=";
        };
      in
      runCommand "ggl-src" { } ''
        cp -r ${filtered-src} $out
        chmod -R +w $out
        rm -rf $out/deps/core_mqtt/coreMQTT
        ln -s ${core_mqtt} $out/deps/core_mqtt/coreMQTT
      '';

    llvmStdenv = final.overrideCC final.llvmPackages.stdenv
      (final.llvmPackages.stdenv.cc.override
        { inherit (final.llvmPackages) bintools; });
  };
}

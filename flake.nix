# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

{
  description = "AWS IoT Greengrass runtime for constrained devices.";
  inputs.flakelight.url = "github:accelbread/flakelight";
  outputs = { flakelight, ... }: flakelight ./. (
    let
      llvmStdenvFor = pkgs: with pkgs; overrideCC llvmPackages.stdenv
        (llvmPackages.stdenv.cc.override
          { inherit (llvmPackages) bintools; });

      core_mqtt = { fetchgit, ... }: fetchgit {
        url = "https://github.com/FreeRTOS/coreMQTT.git";
        rev = "58d626a25839ed20b19a57fdff34a41f36dacd05";
        hash = "sha256-QJyK0LGPCFMx6Q1/VyNBS8m5ywAiQcbgLdPRkA3+ilw=";
      };
    in
    rec {
      pname = "ggl";
      package =
        { pkgs, lib, stdenv, fetchgit, runCommand, pkg-config, openssl, defaultMeta }:
        let
          filtered-src = lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.fileFilter
              (file: file.name == "Makefile"
                || lib.any file.hasExt [ "c" "h" "mk" ]) ./.;
          };
          src-with-modules = runCommand "ggl-src" { } ''
            cp -r ${filtered-src} $out
            chmod -R +w $out
            ln -s ${core_mqtt pkgs} $out/deps/core_mqtt/coreMQTT
          '';
        in
        stdenv.mkDerivation {
          name = "ggl";
          src = src-with-modules;
          nativeBuildInputs = [ pkg-config ];
          buildInputs = [ openssl ];
          preConfigure = "export AR=gcc-ar";
          installFlags = [ "DESTDIR=$(out)" ];
          meta = defaultMeta;
        };

      packages.ggl-clang = { default, pkgs }:
        (default.overrideAttrs {
          preConfigure = "";
        }).override { stdenv = llvmStdenvFor pkgs; };

      devShell = {
        shellHook = "export AR=gcc-ar";
        packages = pkgs: with pkgs; [
          pkg-config
          clang-tools
          bear
          openssl
          git
          git-secrets
        ];
      };

      devShells.clang = devShell // { stdenv = llvmStdenvFor; };

      checks = {
        spelling = pkgs: ''
          ${pkgs.nodePackages.cspell}/bin/cspell "**" --quiet
          ${pkgs.coreutils}/bin/sort -cuf utils/dictionary.txt
        '';
        clang-tidy =
          { pkgs, stdenv, runCommand, pkg-config, bear, clang-tools, openssl, ... }:
          let
            src-with-modules = runCommand "ggl-src" { } ''
              cp -r ${./.} $out
              chmod -R +w $out
              ln -s ${core_mqtt pkgs} $out/deps/core_mqtt/coreMQTT
            '';
          in
          stdenv.mkDerivation {
            name = "check-clang-tidy";
            src = src-with-modules;
            nativeBuildInputs = [ pkg-config bear clang-tools ];
            buildInputs = [ openssl ];
            preConfigure = "export AR=gcc-ar";
            buildPhase = ''
              make PROFILE=debug compile_commands.json
              clang-tidy $(find . -name "*.c" -not -path "./deps/*") \
                --warnings-as-errors='*'
            '';
            installPhase = "touch $out";
          };
      };

      formatters = pkgs: {
        "*.c" = "${pkgs.llvmPackages.clang-unwrapped}/bin/clang-format -i";
        "*.h" = "${pkgs.llvmPackages.clang-unwrapped}/bin/clang-format -i";
      };
    }
  );
}

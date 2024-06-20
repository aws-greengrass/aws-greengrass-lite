# gravel - Utilities for AWS IoT Core clients
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

{
  description = "Utilities for AWS IoT Core clients.";
  inputs.flakelight.url = "github:accelbread/flakelight";
  outputs = { flakelight, ... }: flakelight ./. (
    let
      llvmStdenvFor = pkgs: with pkgs; overrideCC llvmPackages.stdenv
        (llvmPackages.stdenv.cc.override
          { inherit (llvmPackages) bintools; });
    in
    rec {
      pname = "gravel";
      package =
        { lib, stdenv, fetchgit, runCommand, pkg-config, openssl, defaultMeta }:
        let
          core_mqtt = fetchgit {
            url = "https://github.com/FreeRTOS/coreMQTT.git";
            rev = "58d626a25839ed20b19a57fdff34a41f36dacd05";
            hash = "sha256-QJyK0LGPCFMx6Q1/VyNBS8m5ywAiQcbgLdPRkA3+ilw=";
          };
          filtered-src = lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.fileFilter
              (file: file.name == "Makefile"
                || lib.any file.hasExt [ "c" "h" "mk" ]) ./.;
          };
          src-with-modules = runCommand "gravel-src" { } ''
            cp -r ${filtered-src} $out
            chmod -R +w $out
            ln -s ${core_mqtt} $out/deps/core_mqtt/coreMQTT
          '';
        in
        stdenv.mkDerivation {
          name = "gravel";
          src = src-with-modules;
          nativeBuildInputs = [ pkg-config ];
          buildInputs = [ openssl ];
          preConfigure = "export AR=gcc-ar";
          installFlags = [ "DESTDIR=$(out)" ];
          meta = defaultMeta;
        };

      packages.gravel-clang = { default, pkgs }:
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

      formatters = pkgs: {
        "*.c" = "${pkgs.llvmPackages.clang-unwrapped}/bin/clang-format -i";
        "*.h" = "${pkgs.llvmPackages.clang-unwrapped}/bin/clang-format -i";
      };
    }
  );
}

# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

{
  description = "AWS IoT Greengrass runtime for constrained devices.";
  inputs.flakelight.url = "github:nix-community/flakelight";
  outputs = { flakelight, ... }@inputs: flakelight ./.
    ({ lib, config, ... }:
      let
        buildFileset = lib.fileset.unions [
          ./CMakeLists.txt
          ./fc_deps.json
          ./misc/systemd
          ./misc/cmake_uninstall.cmake.in
          ./version
          ./modules
        ];
        buildSrc = lib.fileset.toSource { root = ./.; fileset = buildFileset; };

        checkFileset = lib.fileset.unions [
          buildFileset
          ./.clang-tidy
          ./test_modules
        ];
        checkSrc = lib.fileset.toSource { root = ./.; fileset = checkFileset; };

        deps = pkgs: lib.mapAttrs
          (_: v: pkgs.fetchgit (v // { fetchSubmodules = true; }))
          (builtins.fromJSON (builtins.readFile ./fc_deps.json));

        fetchContentFlags = pkgs: lib.mapAttrsToList
          (n: v: "-DFETCHCONTENT_SOURCE_DIR_${lib.toUpper n}=${v}")
          (deps pkgs);

        llvmStdenv = pkgs: pkgs.overrideCC pkgs.llvmPackages.stdenv
          (pkgs.llvmPackages.stdenv.cc.override
            { inherit (pkgs.llvmPackages) bintools; });
      in
      {
        inherit inputs;
        systems = [ "x86_64-linux" "aarch64-linux" ];

        devShell = pkgs: {
          packages = with pkgs; [
            clang-tools
            clangd-tidy
            git
            gdb
          ];
          env.NIX_HARDENING_ENABLE = "";
          shellHook = ''
            export MAKEFLAGS=-j
          '';
        };

        devShells.clang = { lib, pkgs, ... }: {
          imports = [ (config.devShell pkgs) ];
          stdenv = lib.mkForce (llvmStdenv pkgs);
        };

        formatters = { llvmPackages, cmake-format, nodePackages, yapf, ... }:
          let
            fmt-c = "${llvmPackages.clang-unwrapped}/bin/clang-format -i";
            fmt-cmake = "${cmake-format}/bin/cmake-format -i";
            fmt-yaml =
              "${nodePackages.prettier}/bin/prettier --write --parser yaml";
          in
          {
            "*.c" = fmt-c;
            "*.h" = fmt-c;
            "CMakeLists.txt" = fmt-cmake;
            "*.cmake" = fmt-cmake;
            ".clang*" = fmt-yaml;
            "*.sql" = "${nodePackages.sql-formatter}/bin/sql-formatter --fix " +
              "--config ${./.sql-formatter.json}";
            "*.py" = "${yapf}/bin/yapf -i";
          };

        pname = "ggl";
        package =
          { pkgs
          , lib
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
          }:
          stdenv.mkDerivation {
            name = "aws-greengrass-lite";
            src = buildSrc;
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
            cmakeFlags = (fetchContentFlags pkgs) ++ [ "-DENABLE_WERROR=1" ];
          };

        checks =
          let
            clangBuildDir = { pkgs, pkg-config, clang-tools, cmake, ... }:
              (llvmStdenv pkgs).mkDerivation {
                name = "clang-cmake-build-dir";
                nativeBuildInputs = [ pkg-config clang-tools ];
                inherit (pkgs.ggl) buildInputs;
                buildPhase = ''
                  ${cmake}/bin/cmake -B $out -S ${checkSrc} \
                    -D BUILD_EXAMPLES=1 -D CMAKE_BUILD_TYPE=Debug \
                    ${toString (fetchContentFlags pkgs)}
                  rm $out/CMakeFiles/CMakeConfigureLog.yaml
                '';
                dontUnpack = true;
                dontPatch = true;
                dontConfigure = true;
                dontInstall = true;
                dontFixup = true;
                allowSubstitutes = false;
              };
          in
          {
            build-clang = pkgs: pkgs.ggl.override
              { stdenv = llvmStdenv pkgs; };
            # cspell:ignore muslpi
            build-musl-pi = pkgs: pkgs.pkgsCross.muslpi.ggl;

            clang-tidy = { pkgs, clang-tools, clangd-tidy, fd, ... }:
              (llvmStdenv pkgs).mkDerivation {
                name = "check-clangd-tidy";
                nativeBuildInputs = [ clang-tools clangd-tidy fd ];
                inherit (pkgs.ggl) buildInputs;
                buildPhase = ''
                  set -eo pipefail
                  cd ${checkSrc}
                  clangd-tidy -j$(nproc) -p ${clangBuildDir pkgs} \
                    --color=always $(fd . modules -e c -e h)
                    touch $out
                '';
                dontUnpack = true;
                dontPatch = true;
                dontConfigure = true;
                dontInstall = true;
                dontFixup = true;
                allowSubstitutes = false;
              };

            iwyu = { pkgs, include-what-you-use, fd, ... }:
              (llvmStdenv pkgs).mkDerivation {
                name = "check-iwyu";
                nativeBuildInputs = [ include-what-you-use fd ];
                inherit (pkgs.ggl) buildInputs;
                buildPhase = ''
                  set -eo pipefail
                  white=$(printf "\e[1;37m")
                  red=$(printf "\e[1;31m")
                  clear=$(printf "\e[0m")
                  iwyu_tool.py -o clang -j $(nproc) -p ${clangBuildDir pkgs} \
                    $(fd . ${checkSrc}/ -e c) -- \
                    -Xiwyu --error -Xiwyu --check_also="${checkSrc}/*" \
                    -Xiwyu --mapping_file=${./.}/misc/iwyu_mappings.yml |\
                    { grep error: || true; } |\
                    sed 's|\(.*\)error:\(.*\)|'$white'\1'$red'error:'$white'\2'$clear'|' |\
                    sed 's|${checkSrc}/||'
                  touch $out
                '';
                dontUnpack = true;
                dontPatch = true;
                dontConfigure = true;
                dontInstall = true;
                dontFixup = true;
                allowSubstitutes = false;
              };

            cmake-lint = pkgs: ''
              ${pkgs.cmake-format}/bin/cmake-lint \
                $(${pkgs.fd}/bin/fd '.*\.cmake|CMakeLists.txt') \
                --suppress-decorations
            '';

            spelling = pkgs: ''
              ${pkgs.nodePackages.cspell}/bin/cspell "**" --quiet
              ${pkgs.coreutils}/bin/sort -cuf misc/dictionary.txt
            '';

            shellcheck = pkgs: ''
              ${pkgs.shellcheck}/bin/shellcheck $(${pkgs.fd}/bin/fd -e sh -e bash)
            '';
          };

        withOverlays = final: prev: {
          clangd-tidy = final.callPackage
            ({ python3Packages }:
              python3Packages.buildPythonPackage rec {
                pname = "clangd_tidy";
                version = "1.1.0.post2";
                format = "pyproject";
                src = final.fetchPypi {
                  inherit pname version;
                  hash = "sha256-NyghLY+BeY9LAOstKEFcPLdA7l1jCdHLuyPms4bOyYE=";
                };
                buildInputs = with python3Packages; [ setuptools-scm ];
                propagatedBuildInputs = with python3Packages; [
                  attrs
                  cattrs
                  typing-extensions
                ];
              })
            { };
        };

        legacyPackages = pkgs: {
          _type = "pkgs";
          cached-paths = pkgs.stdenv.mkDerivation {
            name = "cached-paths";
            exportReferencesGraph =
              let
                getAttrSet = name: pkgs.lib.mapAttrs'
                  (n: pkgs.lib.nameValuePair ("${name}-" + n))
                  pkgs.outputs'.${name};
                cached-outputs = pkgs.linkFarm "cached-outputs" (
                  (getAttrSet "packages") //
                  (getAttrSet "devShells") //
                  (getAttrSet "checks") //
                  { "formatter" = pkgs.outputs'.formatter; }
                );
              in
              [ "cache-paths" cached-outputs.drvPath ];
            buildPhase =
              "grep '^/nix/store/' < cache-paths | sort | uniq > $out";
            dontUnpack = true;
            dontPatch = true;
            dontConfigure = true;
            dontInstall = true;
            dontFixup = true;
            allowSubstitutes = false;
          };
        };
      });
}

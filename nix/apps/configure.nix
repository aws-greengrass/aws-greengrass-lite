pkgs: ''
  nix develop -c cmake -Bbuild \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX=$PWD/install \
    ${toString pkgs.default.fetchcontentFlags}
''

pkgs: ''
  ${pkgs.cmake-format}/bin/cmake-lint \
    $(${pkgs.fd}/bin/fd '.*\.cmake|CMakeLists.txt') \
    --suppress-decorations
''

{
  cmake-lint = pkgs: ''
    ${pkgs.cmake-format}/bin/cmake-lint \
      $(${pkgs.fd}/bin/fd '.*\.cmake|CMakeLists.txt') \
      -c .cmake-format.yml --suppress-decorations
  '';
}

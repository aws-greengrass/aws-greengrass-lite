{ src, pkgs, ... }:
let
  clang-format = "${pkgs.clang-tools_17}/bin/clang-format -i";
  cmake-format = "${pkgs.cmake-format}/bin/cmake-format " +
    "-c ${src}/.cmake-format.yml -i";
  yapf = "${pkgs.yapf}/bin/yapf -i";
in
{
  "*.c | *.h | *.cpp | *.hpp" = clang-format;
  "*.cmake | *CMakeLists.txt" = cmake-format;
  "*.py" = yapf;
}

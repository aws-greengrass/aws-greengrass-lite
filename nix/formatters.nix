{ llvmPackages, cmake-format, yapf, nodePackages, ... }:
let
  fmt-c = "${llvmPackages.clang-unwrapped}/bin/clang-format -i";
  fmt-cmake = "${cmake-format}/bin/cmake-format -i";
  fmt-yaml = "${nodePackages.prettier}/bin/prettier --write --parser yaml";
in
{
  "*.c" = fmt-c;
  "*.h" = fmt-c;
  "*.cmake" = fmt-cmake;
  "CMakeLists.txt" = fmt-cmake;
  "*.py" = "${yapf}/bin/yapf -i";
  ".clang*" = fmt-yaml;
}

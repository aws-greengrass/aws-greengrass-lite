{ llvmPackages, cmake-format, ... }:
let
  fmt-c = "${llvmPackages.clang-unwrapped}/bin/clang-format -i";
  fmt-cmake = "${cmake-format}/bin/cmake-format -i";
in
{
  "*.c" = fmt-c;
  "*.h" = fmt-c;
  "*.cmake" = fmt-cmake;
  "CMakeLists.txt" = fmt-cmake;
}

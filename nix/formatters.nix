{ src, ... }: {
  "*.c | *.h | *.cpp | *.hpp" = "clang-format -i";
  "*.cmake | *CMakeLists.txt" = "cmake-format -c ${src}/.cmake-format.yml -i";
  "*.py" = "yapf -i";
}

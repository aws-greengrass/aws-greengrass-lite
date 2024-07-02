{ llvmPackages, ... }: {
  "*.c" = "${llvmPackages.clang-unwrapped}/bin/clang-format -i";
  "*.h" = "${llvmPackages.clang-unwrapped}/bin/clang-format -i";
}

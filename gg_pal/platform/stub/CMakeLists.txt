add_library(gg_pal STATIC include/gg_pal/types.hpp src/process.cpp)

target_include_directories(
  gg_pal
  PUBLIC include
  PRIVATE src)
target_link_libraries(gg_pal PUBLIC gg_pal_interface)

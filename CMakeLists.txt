cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_SOURCE_DIR}/cmake/modules"
)

find_package(Clang REQUIRED libTooling)
find_package(LLVM REQUIRED)

add_definitions(
-D__STDC_LIMIT_MACROS
-D__STDC_CONSTANT_MACROS
)

add_executable(main main.cpp)
target_link_libraries(main
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangTooling
  clangLex
  LLVM
)

cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_SOURCE_DIR}/cmake/modules"
)

enable_testing()

find_package(Clang REQUIRED libTooling)
find_package(LLVM REQUIRED)

add_definitions(
-D__STDC_LIMIT_MACROS
-D__STDC_CONSTANT_MACROS
)

set(NAME headless)
add_executable(${NAME} main.cpp header.hpp header.cpp)
target_link_libraries(${NAME}
  clangAST
  clangBasic
  clangFrontend
  clangTooling
  clangLex
  LLVM
  stdc++fs
)

set_target_properties(${NAME} PROPERTIES
  CXX_STANDARD 17)

file(GLOB integration_tests "tests/*")
list(LENGTH integration_tests tests_num)
MATH(EXPR max_test_index "${tests_num} - 1")

foreach(test_id RANGE ${max_test_index})
  list(GET integration_tests ${test_id} test_dir)
  set(test_source "${test_dir}/test.cpp")
  set(test_expected "${test_dir}/test.out")

  # Strip path
  string(REGEX REPLACE "/.*/" "" test_name ${test_dir})

  set(comparison_command sh -c "$<TARGET_FILE:${NAME}> ${test_source} | diff - ${test_expected}")
  add_test(NAME test_${test_name} COMMAND ${comparison_command})
endforeach(test_id)

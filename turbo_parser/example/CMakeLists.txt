cmake_add_executable(turbo_parser_query_example_c
  SOURCES turbo_parser_query_example.c
  LIBS turbo_parser
  FOLDER "turbo_parser/example")

set_target_properties(turbo_parser_query_example_c PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

cmake_add_executable(turbo_parser_query_example_cpp
  SOURCES turbo_parser_query_example.cpp
  LIBS turbo_parser
  FOLDER "turbo_parser/example")
set_target_properties(turbo_parser_query_example_cpp PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


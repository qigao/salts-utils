# Phase A: algorithm-only C11. No installed/public API until A4 conformance.
# The module has no filesystem, CFlow, Rust, TRE or external process dependency.
add_library(salts_search_algorithms STATIC
  src/search_literal.c
  src/search_lines.c
  src/search_glob.c
  src/search_ignore.c)
target_compile_features(salts_search_algorithms PUBLIC c_std_11)
set_target_properties(salts_search_algorithms PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  FOLDER "search")
target_include_directories(salts_search_algorithms PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(BUILD_TESTS)
  cmake_add_test(test_salts_search_literal
    SOURCES tests/test_search_literal.c
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src"
    LIBS salts_search_algorithms Salts::TinyTest
    FOLDER "search/tests")
  cmake_add_test(test_salts_search_lines
    SOURCES tests/test_search_lines.c
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src"
    LIBS salts_search_algorithms Salts::TinyTest
    FOLDER "search/tests")
  cmake_add_test(test_salts_search_glob
    SOURCES tests/test_search_glob.c
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src"
    LIBS salts_search_algorithms Salts::TinyTest
    FOLDER "search/tests")
  cmake_add_test(test_salts_search_ignore
    SOURCES tests/test_search_ignore.c
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src"
    LIBS salts_search_algorithms Salts::TinyTest
    FOLDER "search/tests")
endif()

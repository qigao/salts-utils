cmake_add_benchmark(benchmark_jinja_cmeta
  SOURCES benchmark_jinja_cmeta.c
  LIBS Salts::JinjaCMeta Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "jinja/benchmarks")

if(BUILD_TESTS)
  add_test(NAME benchmark_jinja_cmeta COMMAND benchmark_jinja_cmeta)
  cmake_set_test_runtime_path(benchmark_jinja_cmeta benchmark_jinja_cmeta)
  set_tests_properties(benchmark_jinja_cmeta PROPERTIES LABELS "bench;jinja;cmeta")
endif()

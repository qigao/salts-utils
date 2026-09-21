cmake_minimum_required(VERSION 3.27)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR
    "DataBind/TBE are SaltsUtils components; configure the SaltsUtils repository root")
endif()

set(DATABIND_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${DATABIND_SOURCE_ROOT}/cmake/DataBindTools.cmake")
include("${DATABIND_SOURCE_ROOT}/cmake/DataBindBuild.cmake")

add_subdirectory(vendor/monocypher)
add_subdirectory(schema)
add_subdirectory(data_bind)
add_subdirectory(tbe_compiler)

if(BUILD_TESTING)
  add_subdirectory(tests/package_config)
  add_subdirectory(tests/native_storage)
endif()

include("${DATABIND_SOURCE_ROOT}/cmake/DataBindPackage.cmake")

# Benchmark helpers create executable targets unconditionally. Keep the TBE
# aggregate build faithful to BUILD_BENCHMARKS so CI gates configured with
# BUILD_BENCHMARKS=OFF do not compile stale/non-gating benchmark consumers.
if(NOT BUILD_BENCHMARKS)
  foreach(benchmark_target
          benchmark_tbe_simple
          benchmark_encoding
          benchmark_data_bind_pool
          benchmark_data_bind_json_path
          benchmark_tbe_typed)
    if(TARGET ${benchmark_target})
      set_target_properties(${benchmark_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
  endforeach()
endif()

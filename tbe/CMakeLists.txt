set(DATABIND_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(schema)
add_subdirectory(data_bind)
add_subdirectory(tbe_compiler)

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

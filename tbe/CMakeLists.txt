cmake_minimum_required(VERSION 3.27)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR
    "TBE/DataBind are SaltsUtils components; configure the SaltsUtils repository root")
endif()

set(DATABIND_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(vendor/monocypher)
add_subdirectory(schema)
add_subdirectory(data_bind)
add_subdirectory(tbe_compiler)

if(BUILD_TESTS)
  add_subdirectory(tests/package_config)
  add_subdirectory(tests/native_storage)
endif()

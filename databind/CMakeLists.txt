cmake_minimum_required(VERSION 3.27)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR
    "DataBind is a SaltsUtils component; configure the SaltsUtils repository root")
endif()

set(DATABIND_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(formats/binary)
add_subdirectory(schema)
add_subdirectory(idl)
add_subdirectory(compiler)
add_subdirectory(runtime)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

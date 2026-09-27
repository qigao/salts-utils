cmake_minimum_required(VERSION 3.27)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR
    "DataBind is a SaltsUtils component; configure the SaltsUtils repository root")
endif()

set(DATABIND_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(vendor/monocypher)
add_subdirectory(formats/tbe)
add_subdirectory(schema)
target_link_libraries(databind_tbe_contract
  PUBLIC Salts::IDL
  PRIVATE Salts::Core)
add_subdirectory(runtime)
add_subdirectory(compiler)

if(BUILD_TESTS)
  add_subdirectory(tests/package_config)
  add_subdirectory(tests/native_storage)
endif()

cmake_minimum_required(VERSION 3.27)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR
    "DataBind is a SaltsUtils component; configure the SaltsUtils repository root")
endif()

set(DATABIND_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(vendor/monocypher)
add_subdirectory(formats/binary)
add_subdirectory(schema)
add_subdirectory(idl)
add_subdirectory(binding/compiler)
add_subdirectory(producer)
target_link_libraries(databind_binary_contract
  PUBLIC Salts::IDL Salts::CSerde
  PRIVATE Salts::Schema Salts::Core)
add_subdirectory(runtime)
add_subdirectory(idl/compiler)

if(BUILD_TESTS)
  add_subdirectory(tests/package_config)
  add_subdirectory(tests/native_storage)
endif()

cmake_minimum_required(VERSION 3.27)
project(DataBindGeneratedABIInstalledConsumer LANGUAGES C)

find_package(SaltsUtils CONFIG REQUIRED)

if(NOT TARGET Salts::DataBindGeneratedABI)
  message(FATAL_ERROR "missing Salts::DataBindGeneratedABI")
endif()
get_target_property(_generated_abi_type Salts::DataBindGeneratedABI TYPE)
if(NOT _generated_abi_type STREQUAL "INTERFACE_LIBRARY")
  message(FATAL_ERROR
          "Salts::DataBindGeneratedABI must be an INTERFACE_LIBRARY")
endif()

add_executable(databind_generated_abi_consumer main.c)
target_compile_features(databind_generated_abi_consumer PRIVATE c_std_11)
target_link_libraries(databind_generated_abi_consumer
  PRIVATE Salts::DataBindGeneratedABI)

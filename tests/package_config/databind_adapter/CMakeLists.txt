cmake_minimum_required(VERSION 3.27)

project(databind_package_adapter_consumer LANGUAGES C)

file(TO_CMAKE_PATH "$ENV{DATABIND_ROOT}" DATABIND_ROOT_PATH)
find_package(DataBind 3 CONFIG REQUIRED COMPONENTS JsonAdapter
  PATHS "${DATABIND_ROOT_PATH}" NO_DEFAULT_PATH)

if(NOT TARGET DataBind::JsonAdapter)
  message(FATAL_ERROR "DataBind::JsonAdapter is missing")
endif()
if(NOT TARGET Salts::JsonCSerdeAdapter)
  message(FATAL_ERROR
    "JsonAdapter did not resolve its explicit SaltsUtils parser dependency")
endif()

add_library(databind_adapter_consumer STATIC main.c)
target_link_libraries(databind_adapter_consumer PRIVATE DataBind::JsonAdapter)

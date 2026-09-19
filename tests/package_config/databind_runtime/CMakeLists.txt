cmake_minimum_required(VERSION 3.27)

project(databind_package_runtime_consumer LANGUAGES C)

if(NOT DEFINED ENV{DATABIND_ROOT} OR "$ENV{DATABIND_ROOT}" STREQUAL "")
  message(FATAL_ERROR "DATABIND_ROOT is required")
endif()
file(TO_CMAKE_PATH "$ENV{DATABIND_ROOT}" DATABIND_ROOT_PATH)

find_package(DataBind 3 CONFIG REQUIRED COMPONENTS Runtime
  PATHS "${DATABIND_ROOT_PATH}" NO_DEFAULT_PATH)

if(NOT TARGET DataBind::Runtime)
  message(FATAL_ERROR "DataBind::Runtime is missing")
endif()
foreach(forbidden_target IN ITEMS
    DataBind::JsonAdapter DataBind::YamlAdapter DataBind::CsvAdapter
    DataBind::XmlAdapter DataBind::TemporalAdapter
    Salts::JsonCSerdeAdapter Salts::CYamlJsonAdapter Salts::CsvParser
    Salts::XmlParser Salts::DateTimeParser)
  if(TARGET ${forbidden_target})
    message(FATAL_ERROR
      "Core/runtime-only DataBind discovery imported concrete adapter target: ${forbidden_target}")
  endif()
endforeach()

if(TARGET DataBind::CFlow)
  message(FATAL_ERROR "Runtime discovery unexpectedly imported DataBind::CFlow")
endif()

add_executable(databind_runtime_consumer main.c)
target_link_libraries(databind_runtime_consumer PRIVATE DataBind::Runtime)

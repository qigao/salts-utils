cmake_minimum_required(VERSION 3.27)

project(databind_package_core_consumer LANGUAGES C)

if(NOT DEFINED ENV{DATABIND_ROOT} OR "$ENV{DATABIND_ROOT}" STREQUAL "")
  message(FATAL_ERROR "DATABIND_ROOT is required")
endif()
file(TO_CMAKE_PATH "$ENV{DATABIND_ROOT}" DATABIND_ROOT_PATH)
if(NOT IS_DIRECTORY "${DATABIND_ROOT_PATH}")
  message(FATAL_ERROR
    "DATABIND_ROOT is not a directory: $ENV{DATABIND_ROOT}")
endif()

find_package(DataBind 3 CONFIG REQUIRED COMPONENTS Core
  PATHS "${DATABIND_ROOT_PATH}" NO_DEFAULT_PATH)

foreach(required_target IN ITEMS DataBind::Core DataBind::Schema DataBind::Runtime DataBind::CMeta)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR "DataBind package did not import ${required_target}")
  endif()
endforeach()

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
  message(FATAL_ERROR
    "Core-only DataBind discovery must not import the CFlow adapter")
endif()

foreach(legacy_target IN ITEMS
    Salts::DataBind Salts::DataBindCore Salts::DataBindCMeta
    Salts::DataBindCFlow Salts::TbeSchema Salts::SchemaBE)
  if(TARGET ${legacy_target})
    message(FATAL_ERROR
      "Independent DataBind package exposed legacy target: ${legacy_target}")
  endif()
endforeach()

add_library(databind_core_consumer STATIC main.c)
target_link_libraries(databind_core_consumer PRIVATE DataBind::Core)

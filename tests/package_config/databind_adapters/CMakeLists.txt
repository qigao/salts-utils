cmake_minimum_required(VERSION 3.27)

project(databind_package_config_adapters LANGUAGES C CXX)

if(NOT IS_DIRECTORY "${DATABIND_PACKAGE_DIR}")
  message(FATAL_ERROR
    "DATABIND_PACKAGE_DIR is not a directory: ${DATABIND_PACKAGE_DIR}")
endif()

find_package(DataBind CONFIG REQUIRED
  PATHS "${DATABIND_PACKAGE_DIR}" NO_DEFAULT_PATH)

find_package(DataBind CONFIG REQUIRED COMPONENTS
  JsonAdapter YamlAdapter CsvAdapter XmlAdapter TemporalAdapter
  PATHS "${DATABIND_PACKAGE_DIR}" NO_DEFAULT_PATH)

foreach(required_target IN ITEMS
    Salts::DataBindJsonAdapter
    Salts::DataBindYamlAdapter
    Salts::DataBindCsvAdapter
    Salts::DataBindXmlAdapter
    Salts::DataBindTemporalAdapter)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "DataBind adapter package is missing target ${required_target}")
  endif()
endforeach()

if(NOT TARGET Salts::JsonParser OR
   NOT TARGET Salts::XmlParser OR
   NOT TARGET Salts::CsvParser OR
   NOT TARGET Salts::DateTimeParser)
  message(FATAL_ERROR
    "DataBind adapter discovery did not resolve the explicit SaltsUtils dependency")
endif()

cmake_minimum_required(VERSION 3.27)

project(databind_package_config_adapters LANGUAGES C CXX)

if(NOT IS_DIRECTORY "${SALTS_UTILS_PACKAGE_DIR}")
  message(FATAL_ERROR
    "SALTS_UTILS_PACKAGE_DIR is not a directory: ${SALTS_UTILS_PACKAGE_DIR}")
endif()

find_package(SaltsUtils CONFIG REQUIRED
  PATHS "${SALTS_UTILS_PACKAGE_DIR}" NO_DEFAULT_PATH)
find_package(SaltsUtils CONFIG REQUIRED)

foreach(required_target IN ITEMS
    Salts::DataBindCore
    Salts::DataBindJsonAdapter
    Salts::DataBindYamlAdapter
    Salts::DataBindCsvAdapter
    Salts::DataBindXmlAdapter
    Salts::DataBindTemporalAdapter)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "SaltsUtils DataBind component is missing target ${required_target}")
  endif()
endforeach()

if(NOT TARGET Salts::JsonParser OR
   NOT TARGET Salts::XmlParser OR
   NOT TARGET Salts::CsvParser OR
   NOT TARGET Salts::DateTimeParser)
  message(FATAL_ERROR
    "SaltsUtils is missing the DataBind adapters' parser dependencies")
endif()

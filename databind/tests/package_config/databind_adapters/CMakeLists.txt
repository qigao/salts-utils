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


include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES Salts::DataBindJsonAdapter)
check_c_source_compiles([=[
#include <data_bind_json_provider.h>
#include <json_cserde_writer.h>

static int sink_write(const void *data, size_t len, void *user) {
  (void)data;
  (void)len;
  (void)user;
  return 0;
}

int main(void) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  const DataBindFormatProvider *provider = data_bind_json_format_provider();
  if (provider == 0) return 1;
  if (data_bind_format_writer_open(
          provider, sink_write, 0, 4u, &lease, &error) != DATA_BIND_OK)
    return 2;
  return data_bind_format_writer_close(&lease, &error) == DATA_BIND_OK ? 0 : 3;
}
]=] DATABIND_JSON_WRITER_CONSUMER_COMPILES)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT DATABIND_JSON_WRITER_CONSUMER_COMPILES)
  message(FATAL_ERROR
    "Installed Salts::DataBindJsonAdapter writer consumer failed to compile/link")
endif()

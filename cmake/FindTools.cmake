set(RE2C_MIN_VERSION "4.6")
set(RE2C_UNICODE_PROPERTIES_SHA256
    "0980dcc0aad753cb3011f0e5700d056a8198df039492e6813c4f88993ea44f63")

find_program(RE2C_EXECUTABLE NAMES re2c REQUIRED)
execute_process(
  COMMAND "${RE2C_EXECUTABLE}" --version
  RESULT_VARIABLE RE2C_VERSION_RESULT
  OUTPUT_VARIABLE RE2C_VERSION_OUTPUT
  ERROR_VARIABLE RE2C_VERSION_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT RE2C_VERSION_RESULT EQUAL 0 OR
   NOT RE2C_VERSION_OUTPUT MATCHES "re2c ([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
  message(FATAL_ERROR
          "Unable to determine re2c version from '${RE2C_EXECUTABLE}': ${RE2C_VERSION_ERROR}")
endif()
set(RE2C_VERSION "${CMAKE_MATCH_1}")
if(RE2C_VERSION VERSION_LESS RE2C_MIN_VERSION)
  message(FATAL_ERROR
          "re2c ${RE2C_MIN_VERSION} or newer is required; found ${RE2C_VERSION} at ${RE2C_EXECUTABLE}")
endif()

if(NOT RE2C_STDLIB_DIR)
  get_filename_component(_re2c_bin_dir "${RE2C_EXECUTABLE}" DIRECTORY)
  get_filename_component(_re2c_prefix "${_re2c_bin_dir}" DIRECTORY)
  set(RE2C_STDLIB_DIR "${_re2c_prefix}/share/re2c/stdlib" CACHE PATH
      "Directory containing re2c standard include files")
endif()
set(RE2C_UNICODE_PROPERTIES "${RE2C_STDLIB_DIR}/unicode_properties.re")
set(RE2C_UNICODE_CATEGORIES "${RE2C_STDLIB_DIR}/unicode_categories.re")
if(NOT EXISTS "${RE2C_UNICODE_CATEGORIES}")
  message(FATAL_ERROR "re2c Unicode category definitions are missing: ${RE2C_UNICODE_CATEGORIES}")
endif()
file(SHA256 "${RE2C_UNICODE_CATEGORIES}" _re2c_unicode_categories_sha256)
if(NOT _re2c_unicode_categories_sha256 STREQUAL
    "56562ef44b0adef04258f59f8b6cd83d3936f7438f0d8162b5c0ccf30c05cf9f")
  message(FATAL_ERROR "Unsupported re2c Unicode category data; expected Unicode 17.0.0 from re2c 4.6")
endif()
if(NOT EXISTS "${RE2C_UNICODE_PROPERTIES}")
  message(FATAL_ERROR
          "re2c Unicode property definitions are missing: ${RE2C_UNICODE_PROPERTIES}")
endif()
file(SHA256 "${RE2C_UNICODE_PROPERTIES}" _re2c_unicode_properties_sha256)
if(NOT _re2c_unicode_properties_sha256 STREQUAL RE2C_UNICODE_PROPERTIES_SHA256)
  message(FATAL_ERROR
          "Unsupported re2c Unicode property data at ${RE2C_UNICODE_PROPERTIES}; "
          "expected the Unicode 17.0.0 definitions shipped by re2c 4.6")
endif()

if(NOT TARGET lemon)
  message(FATAL_ERROR "The required in-tree lemon target is missing")
endif()

set(LEMPAR "${PROJECT_SOURCE_DIR}/tools/lemon/lempar.c")
if(NOT EXISTS "${LEMPAR}")
  message(FATAL_ERROR "The required lemon template is missing: ${LEMPAR}")
endif()

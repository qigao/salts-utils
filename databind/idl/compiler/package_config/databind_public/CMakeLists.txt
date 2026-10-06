cmake_minimum_required(VERSION 3.27)
project(DataBindPublicInstalledConsumer LANGUAGES C)

enable_testing()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
  message(FATAL_ERROR "SALTS_UTILS_ROOT must name the installed SaltsUtils SDK")
endif()
unset(SaltsUtils_DIR CACHE)
file(TO_CMAKE_PATH "$ENV{SALTS_UTILS_ROOT}" SALTS_UTILS_SDK_PATH)
find_package(SaltsUtils CONFIG REQUIRED PATHS "${SALTS_UTILS_SDK_PATH}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

if(NOT TARGET Salts::DataBind)
  message(FATAL_ERROR "missing canonical Salts::DataBind")
endif()
if(TARGET Salts::DataBindGeneratedABI)
  message(FATAL_ERROR
          "removed Salts::DataBindGeneratedABI must not be exported")
endif()

add_executable(databind_public_consumer main.c)
target_compile_features(databind_public_consumer PRIVATE c_std_11)
target_link_libraries(databind_public_consumer PRIVATE Salts::DataBind)
add_test(NAME databind_public_consumer COMMAND databind_public_consumer)

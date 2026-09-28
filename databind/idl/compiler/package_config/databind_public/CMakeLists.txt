cmake_minimum_required(VERSION 3.27)
project(DataBindPublicInstalledConsumer LANGUAGES C)

find_package(SaltsUtils CONFIG REQUIRED)

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

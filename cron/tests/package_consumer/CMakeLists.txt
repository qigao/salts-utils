cmake_minimum_required(VERSION 3.20)

project(cron_package_consumer LANGUAGES C)

find_package(SaltsUtils CONFIG REQUIRED PATHS "${SaltsUtils_DIR}" NO_DEFAULT_PATH)

if(NOT TARGET Salts::CBind)
  message(FATAL_ERROR "SaltsUtils must expose its required Salts package and Salts::CBind")
endif()

add_executable(cron_package_consumer main.c)
target_link_libraries(cron_package_consumer PRIVATE Salts::Cron Salts::CBind)

set_target_properties(cron_package_consumer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

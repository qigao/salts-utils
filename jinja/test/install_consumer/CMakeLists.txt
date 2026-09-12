cmake_minimum_required(VERSION 3.27)
project(jinja_install_consumer LANGUAGES C)

if(NOT DEFINED ENV{SALTS_UTILS_ROOT} OR NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
  message(FATAL_ERROR "SALTS_UTILS_ROOT must name the installed SaltsUtils profile")
endif()
find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)

add_executable(jinja_install_consumer consumer.c)
target_link_libraries(jinja_install_consumer PRIVATE Salts::JinjaCMeta)
set_target_properties(jinja_install_consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
enable_testing()
add_test(NAME installed_jinja_render COMMAND jinja_install_consumer)

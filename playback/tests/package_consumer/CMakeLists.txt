cmake_minimum_required(VERSION 3.27)
project(salts_playback_package_consumer LANGUAGES C)

find_package(SaltsUtils CONFIG REQUIRED PATHS "${SaltsUtils_DIR}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

if(NOT TARGET Salts::Playback)
  message(FATAL_ERROR "SaltsUtils package does not export Salts::Playback")
endif()

add_executable(salts_playback_package_consumer main.c)
target_link_libraries(salts_playback_package_consumer PRIVATE Salts::Playback)
enable_testing()
add_test(NAME salts_playback_package_consumer
         COMMAND salts_playback_package_consumer)

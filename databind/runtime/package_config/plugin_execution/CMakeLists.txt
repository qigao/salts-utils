cmake_minimum_required(VERSION 3.20)
project(databind_plugin_execution_installed LANGUAGES C CXX)

enable_testing()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
  message(FATAL_ERROR "SALTS_UTILS_ROOT must name the installed SaltsUtils SDK")
endif()
unset(SaltsUtils_DIR CACHE)
find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

if(NOT TARGET Salts::DataBindPlugin)
  message(FATAL_ERROR "installed SaltsUtils must export Salts::DataBindPlugin")
endif()

get_target_property(_links Salts::DataBindPlugin INTERFACE_LINK_LIBRARIES)
if(NOT "Salts::DataBind" IN_LIST _links)
  message(FATAL_ERROR "Salts::DataBindPlugin must expose Salts::DataBind")
endif()
if(NOT "Salts::Plugin" IN_LIST _links)
  message(FATAL_ERROR "Salts::DataBindPlugin must expose Salts::Plugin")
endif()

add_executable(databind_plugin_execution_c main.c)
target_link_libraries(databind_plugin_execution_c PRIVATE Salts::DataBindPlugin)
target_compile_features(databind_plugin_execution_c PRIVATE c_std_11)

add_executable(databind_plugin_execution_cpp main.cpp)
target_link_libraries(databind_plugin_execution_cpp PRIVATE Salts::DataBindPlugin)
target_compile_features(databind_plugin_execution_cpp PRIVATE cxx_std_17)
set_target_properties(databind_plugin_execution_cpp PROPERTIES CXX_EXTENSIONS NO)
add_test(NAME databind_plugin_execution_c COMMAND databind_plugin_execution_c)
add_test(NAME databind_plugin_execution_cpp COMMAND databind_plugin_execution_cpp)

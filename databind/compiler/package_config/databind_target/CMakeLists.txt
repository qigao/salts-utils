cmake_minimum_required(VERSION 3.27)
project(DataBindTargetInstalledConsumer LANGUAGES C CXX)

find_package(SaltsUtils CONFIG REQUIRED)

if(NOT TARGET Salts::DataBind)
  message(FATAL_ERROR "missing canonical Salts::DataBind")
endif()
if(TARGET Salts::DataBindGeneratedABI)
  message(FATAL_ERROR
          "removed Salts::DataBindGeneratedABI must not be exported")
endif()

databind_target(
  TARGET installed_image
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/image.schema"
  COMPONENT "Image.ImageProcessor"
  VERSION "1.0.0"
  ARTIFACT_NAME "image"
  PROJECTIONS PLUGIN
  SOURCES business.c)

get_target_property(
  _installed_plugin_links installed_image_plugin LINK_LIBRARIES)
if("Salts::DataBindSchema" IN_LIST _installed_plugin_links)
  message(FATAL_ERROR
          "installed generated Plugin must not link Salts::DataBindSchema")
endif()
if(NOT "Salts::DataBind" IN_LIST _installed_plugin_links)
  message(FATAL_ERROR
          "installed generated Plugin must link canonical Salts::DataBind")
endif()
if("Salts::DataBindGeneratedABI" IN_LIST _installed_plugin_links)
  message(FATAL_ERROR
          "installed generated Plugin must not link removed Salts::DataBindGeneratedABI")
endif()


if(NOT TARGET installed_image_plugin_client)
  message(FATAL_ERROR "missing installed_image_plugin_client")
endif()
get_target_property(
  _installed_client_links installed_image_plugin_client LINK_LIBRARIES)
if("Salts::DataBindSchema" IN_LIST _installed_client_links)
  message(FATAL_ERROR
          "installed generated Plugin client must not link Salts::DataBindSchema")
endif()
if(NOT "Salts::Plugin" IN_LIST _installed_client_links)
  message(FATAL_ERROR
          "installed generated Plugin client must link Salts::Plugin")
endif()
if(NOT "Salts::DataBind" IN_LIST _installed_client_links)
  message(FATAL_ERROR
          "installed generated Plugin client must link canonical Salts::DataBind")
endif()
if("Salts::DataBindGeneratedABI" IN_LIST _installed_client_links)
  message(FATAL_ERROR
          "installed generated Plugin client must not link removed Salts::DataBindGeneratedABI")
endif()

add_executable(installed_image_plugin_client_cpp client_cpp.cpp)
target_compile_features(installed_image_plugin_client_cpp PRIVATE cxx_std_17)
target_link_libraries(installed_image_plugin_client_cpp
  PRIVATE installed_image_plugin_client)
set_target_properties(installed_image_plugin_client_cpp PROPERTIES
  CXX_EXTENSIONS NO)
add_dependencies(installed_image installed_image_plugin_client_cpp)

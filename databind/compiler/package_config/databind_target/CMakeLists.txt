cmake_minimum_required(VERSION 3.27)
project(DataBindTargetInstalledConsumer LANGUAGES C)

find_package(SaltsUtils CONFIG REQUIRED)

if(NOT TARGET Salts::DataBindGeneratedABI)
  message(FATAL_ERROR "missing Salts::DataBindGeneratedABI")
endif()
get_target_property(_generated_abi_type Salts::DataBindGeneratedABI TYPE)
if(NOT _generated_abi_type STREQUAL "INTERFACE_LIBRARY")
  message(FATAL_ERROR
          "Salts::DataBindGeneratedABI must be an INTERFACE_LIBRARY")
endif()

databind_target(
  TARGET installed_image
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/image.schema"
  COMPONENT "Image.ImageProcessor"
  VERSION "1.0.0"
  ARTIFACT_NAME "image"
  PROJECTIONS PLUGIN
  SOURCES business.c)

if(NOT TARGET installed_image_plugin_client)
  message(FATAL_ERROR "missing generated installed_image_plugin_client target")
endif()
get_target_property(
  _installed_client_links installed_image_plugin_client LINK_LIBRARIES)
if("Salts::DataBindSchema" IN_LIST _installed_client_links)
  message(FATAL_ERROR
          "installed generated Plugin client must not link Salts::DataBindSchema")
endif()
if(NOT "Salts::DataBindGeneratedABI" IN_LIST _installed_client_links)
  message(FATAL_ERROR
          "installed generated Plugin client must link Salts::DataBindGeneratedABI")
endif()
if(NOT "Salts::Plugin" IN_LIST _installed_client_links)
  message(FATAL_ERROR
          "installed generated Plugin client must link Salts::Plugin")
endif()

get_target_property(
  _installed_plugin_links installed_image_plugin LINK_LIBRARIES)
if("Salts::DataBindSchema" IN_LIST _installed_plugin_links)
  message(FATAL_ERROR
          "installed generated Plugin must not link Salts::DataBindSchema")
endif()
if(NOT "Salts::DataBindGeneratedABI" IN_LIST _installed_plugin_links)
  message(FATAL_ERROR
          "installed generated Plugin must link Salts::DataBindGeneratedABI")
endif()

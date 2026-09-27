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
  ARTIFACTS PLUGIN
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


databind_target(
  TARGET installed_socket
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/socket.schema"
  ARTIFACT_NAME "installed_device"
  TRANSPORTS SOCKET
  PROJECTION_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/socket_config.json")

add_executable(installed_socket_consumer socket_consumer.c)
target_compile_features(installed_socket_consumer PRIVATE c_std_11)
target_link_libraries(installed_socket_consumer PRIVATE
  Salts::DataBind
  ${installed_socket_NATIVE_TARGET})
target_include_directories(installed_socket_consumer PRIVATE
  "${installed_socket_GENERATED_DIR}")
add_dependencies(installed_socket_consumer
  installed_socket_databind_codegen)
add_dependencies(installed_socket
  installed_socket_consumer)
add_dependencies(installed_image
  installed_socket)


databind_target(
  TARGET installed_flow
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/flowmq.schema"
  ARTIFACT_NAME "installed_flow"
  TRANSPORTS FLOWMQ
  PROJECTION_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/flowmq_config.json")

add_executable(installed_flowmq_consumer flowmq_consumer.c)
target_compile_features(installed_flowmq_consumer PRIVATE c_std_11)
target_link_libraries(installed_flowmq_consumer PRIVATE
  Salts::DataBind
  ${installed_flow_NATIVE_TARGET})
target_include_directories(installed_flowmq_consumer PRIVATE
  "${installed_flow_GENERATED_DIR}")
add_dependencies(installed_flowmq_consumer
  installed_flow_databind_codegen)
add_dependencies(installed_flow
  installed_flowmq_consumer)
add_dependencies(installed_image
  installed_flow)


databind_target(
  TARGET installed_openapi
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/openapi.schema"
  ARTIFACT_NAME "installed_users"
  ARTIFACTS OPENAPI
  TRANSPORTS HTTP
  PROJECTION_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/openapi_config.json")

get_target_property(
  _installed_openapi_artifact
  installed_openapi
  DATABIND_OPENAPI_ARTIFACT)
if(NOT _installed_openapi_artifact)
  message(FATAL_ERROR "installed OpenAPI artifact property is missing")
endif()
if(NOT "${_installed_openapi_artifact}"
       STREQUAL "${installed_openapi_OPENAPI_ARTIFACT}")
  message(FATAL_ERROR
          "installed OpenAPI target property/parent-scope output disagree")
endif()

add_custom_target(installed_openapi_verify
  COMMAND "${CMAKE_COMMAND}"
    "-DOPENAPI_FILE=${_installed_openapi_artifact}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_openapi.cmake"
  VERBATIM)
add_dependencies(installed_openapi_verify
  installed_openapi_databind_codegen)
add_dependencies(installed_openapi
  installed_openapi_verify)
add_dependencies(installed_image
  installed_openapi)

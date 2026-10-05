cmake_minimum_required(VERSION 3.27)
project(DataBindTargetInstalledConsumer LANGUAGES C CXX)

enable_testing()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
  message(FATAL_ERROR "SALTS_UTILS_ROOT must name the installed SaltsUtils SDK")
endif()
unset(SaltsUtils_DIR CACHE)
find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

if(NOT TARGET Salts::DataBind)
  message(FATAL_ERROR "missing canonical Salts::DataBind")
endif()
if(TARGET Salts::DataBindGeneratedABI)
  message(FATAL_ERROR
          "removed Salts::DataBindGeneratedABI must not be exported")
endif()

salts_idl_target(
  TARGET installed_image
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/image.schema"
  COMPONENT "Image.ImageProcessor"
  VERSION "1.0.0"
  ARTIFACT_NAME "image"
  ARTIFACTS PLUGIN
  SOURCES business.c)

get_target_property(
  _installed_plugin_links installed_image_plugin LINK_LIBRARIES)
foreach(frontend_target IN ITEMS Salts::IDL Salts::Schema)
  if("${frontend_target}" IN_LIST _installed_plugin_links)
    message(FATAL_ERROR
            "installed generated Plugin must not link ${frontend_target}")
  endif()
endforeach()
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
foreach(frontend_target IN ITEMS Salts::IDL Salts::Schema)
  if("${frontend_target}" IN_LIST _installed_client_links)
    message(FATAL_ERROR
            "installed generated Plugin client must not link ${frontend_target}")
  endif()
endforeach()
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


salts_idl_target(
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
  installed_socket_idl_codegen)
add_dependencies(installed_socket
  installed_socket_consumer)
add_test(NAME installed_socket_consumer COMMAND installed_socket_consumer)
add_dependencies(installed_image
  installed_socket)

salts_idl_target(
  TARGET installed_message
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/message.schema"
  ARTIFACT_NAME "installed_message"
  ARTIFACTS MESSAGE)

if(NOT TARGET installed_message_native)
  message(FATAL_ERROR "missing installed MESSAGE native target")
endif()
get_target_property(
  _installed_message_artifacts installed_message DATABIND_ARTIFACTS)
if(NOT "MESSAGE" IN_LIST _installed_message_artifacts)
  message(FATAL_ERROR "installed MESSAGE artifact metadata is missing")
endif()

add_executable(installed_message_consumer message_consumer.c)
target_compile_features(installed_message_consumer PRIVATE c_std_11)
target_link_libraries(installed_message_consumer PRIVATE
  Salts::DataBind
  ${installed_message_NATIVE_TARGET})
target_include_directories(installed_message_consumer PRIVATE
  "${installed_message_GENERATED_DIR}")
add_dependencies(installed_message_consumer
  installed_message_idl_codegen)
add_dependencies(installed_message
  installed_message_consumer)

add_executable(installed_message_consumer_cpp message_consumer_cpp.cpp)
target_compile_features(installed_message_consumer_cpp PRIVATE cxx_std_17)
target_link_libraries(installed_message_consumer_cpp PRIVATE
  Salts::DataBind
  ${installed_message_NATIVE_TARGET})
target_include_directories(installed_message_consumer_cpp PRIVATE
  "${installed_message_GENERATED_DIR}")
add_dependencies(installed_message_consumer_cpp
  installed_message_idl_codegen)

add_executable(installed_binary_consumer binary_consumer.c)
target_compile_features(installed_binary_consumer PRIVATE c_std_11)
target_link_libraries(installed_binary_consumer PRIVATE
  Salts::TinyTest Salts::DataBind ${installed_message_NATIVE_TARGET})
target_include_directories(installed_binary_consumer PRIVATE
  "${installed_message_GENERATED_DIR}")
add_dependencies(installed_binary_consumer installed_message_idl_codegen)
add_dependencies(installed_message installed_binary_consumer)
add_test(NAME installed_binary_consumer COMMAND installed_binary_consumer)
set_tests_properties(installed_binary_consumer PROPERTIES
  LABELS "databind;binary;installed;ownership")

add_test(NAME installed_message_consumer COMMAND installed_message_consumer)
add_test(NAME installed_message_consumer_cpp COMMAND installed_message_consumer_cpp)
add_dependencies(installed_message
  installed_message_consumer
  installed_message_consumer_cpp)
add_dependencies(installed_image
  installed_message)

salts_idl_target(
  TARGET installed_service
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/service.schema"
  ARTIFACT_NAME "installed_service"
  ARTIFACTS NATIVE
  TRANSPORTS HTTP RPC)

target_sources(${installed_service_NATIVE_TARGET}
  PRIVATE service_business.c)

if(NOT TARGET installed_service_native)
  message(FATAL_ERROR "missing installed_service native target")
endif()
get_target_property(
  _installed_service_links installed_service_native LINK_LIBRARIES)
if(NOT "Salts::DataBind" IN_LIST _installed_service_links)
  message(FATAL_ERROR
          "installed NATIVE Service target must link canonical Salts::DataBind")
endif()
if(NOT "Salts::CFlow" IN_LIST _installed_service_links)
  message(FATAL_ERROR
          "installed NATIVE Service target must expose canonical Salts::CFlow")
endif()
foreach(frontend_target IN ITEMS Salts::IDL Salts::Schema)
  if("${frontend_target}" IN_LIST _installed_service_links)
    message(FATAL_ERROR
            "installed NATIVE Service target must not link ${frontend_target}")
  endif()
endforeach()

add_executable(installed_service_consumer service_consumer.c)
target_compile_features(installed_service_consumer PRIVATE c_std_11)
target_link_libraries(installed_service_consumer PRIVATE
  Salts::DataBind
  ${installed_service_NATIVE_TARGET})
target_include_directories(installed_service_consumer PRIVATE
  "${installed_service_GENERATED_DIR}")
add_dependencies(installed_service_consumer
  installed_service_idl_codegen)

add_executable(installed_service_consumer_cpp service_consumer_cpp.cpp)
target_compile_features(installed_service_consumer_cpp PRIVATE cxx_std_17)
target_link_libraries(installed_service_consumer_cpp PRIVATE
  Salts::DataBind
  ${installed_service_NATIVE_TARGET})
target_include_directories(installed_service_consumer_cpp PRIVATE
  "${installed_service_GENERATED_DIR}")
add_dependencies(installed_service_consumer_cpp
  installed_service_idl_codegen)

add_dependencies(installed_service
  installed_service_consumer installed_service_consumer_cpp)
add_test(NAME installed_service_consumer COMMAND installed_service_consumer)
add_test(NAME installed_service_consumer_cpp COMMAND installed_service_consumer_cpp)
add_dependencies(installed_image
  installed_service)


salts_idl_target(
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
  installed_openapi_idl_codegen)
add_dependencies(installed_openapi
  installed_openapi_verify)
add_dependencies(installed_image
  installed_openapi)

salts_idl_target(
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
  installed_flow_idl_codegen)
add_dependencies(installed_flow
  installed_flowmq_consumer)
add_test(NAME installed_flowmq_consumer COMMAND installed_flowmq_consumer)
add_dependencies(installed_image
  installed_flow)


salts_idl_target(
  TARGET installed_flow_service
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/service.schema"
  ARTIFACT_NAME "installed_service_flow"
  TRANSPORTS FLOWMQ
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/flowmq_service_config.json")

add_executable(installed_flowmq_service_consumer
  flowmq_service_consumer.c)
target_compile_features(installed_flowmq_service_consumer PRIVATE c_std_11)
target_link_libraries(installed_flowmq_service_consumer PRIVATE
  Salts::DataBind
  ${installed_flow_service_NATIVE_TARGET})
target_include_directories(installed_flowmq_service_consumer PRIVATE
  "${installed_flow_service_GENERATED_DIR}")
add_dependencies(installed_flowmq_service_consumer
  installed_flow_service_idl_codegen)

add_test(NAME installed_flowmq_service_consumer COMMAND installed_flowmq_service_consumer)

add_dependencies(installed_flow_service
  installed_flowmq_service_consumer)
add_dependencies(installed_image
  installed_flow_service)

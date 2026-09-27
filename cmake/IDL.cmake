include_guard(GLOBAL)
include(CMakeParseArguments)

set(SaltsUtils_IDLC_EXECUTABLE ""
    CACHE FILEPATH
    "Host salts-idlc executable used by salts_idl_target()")
set(SaltsUtils_IDLC_HOST_SALTS_ROOT ""
    CACHE PATH
    "Host Salts SDK used to run salts-idlc while cross-compiling")

function(_saltsutils_idl_resolve_compiler
         out_executable out_dependency out_runtime_root out_salts_root)
  if(SaltsUtils_IDLC_EXECUTABLE)
    if(NOT EXISTS "${SaltsUtils_IDLC_EXECUTABLE}")
      message(FATAL_ERROR
              "SaltsUtils_IDLC_EXECUTABLE does not exist: "
              "${SaltsUtils_IDLC_EXECUTABLE}")
    endif()

    get_filename_component(_idlc_bin
      "${SaltsUtils_IDLC_EXECUTABLE}" DIRECTORY)
    get_filename_component(_idlc_prefix
      "${_idlc_bin}" DIRECTORY)

    if(CMAKE_CROSSCOMPILING)
      if(NOT SaltsUtils_IDLC_HOST_SALTS_ROOT)
        message(FATAL_ERROR
                "Cross-compiling salts_idl_target() requires "
                "SaltsUtils_IDLC_HOST_SALTS_ROOT for the host compiler "
                "runtime closure.")
      endif()
      set(_idlc_salts_root
          "${SaltsUtils_IDLC_HOST_SALTS_ROOT}")
    else()
      if(NOT DEFINED ENV{SALTS_ROOT} OR "$ENV{SALTS_ROOT}" STREQUAL "")
        message(FATAL_ERROR
                "SALTS_ROOT must identify the host Salts SDK used by salts-idlc")
      endif()
      file(TO_CMAKE_PATH "$ENV{SALTS_ROOT}" _idlc_salts_root)
    endif()

    set(${out_executable}
        "${SaltsUtils_IDLC_EXECUTABLE}" PARENT_SCOPE)
    set(${out_dependency}
        "${SaltsUtils_IDLC_EXECUTABLE}" PARENT_SCOPE)
    set(${out_runtime_root}
        "${_idlc_prefix}" PARENT_SCOPE)
    set(${out_salts_root}
        "${_idlc_salts_root}" PARENT_SCOPE)
    return()
  endif()

  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
            "salts_idl_target() requires a host salts-idlc while cross-compiling. "
            "Set SaltsUtils_IDLC_EXECUTABLE and "
            "SaltsUtils_IDLC_HOST_SALTS_ROOT.")
  endif()

  if(NOT DEFINED ENV{SALTS_ROOT} OR "$ENV{SALTS_ROOT}" STREQUAL "")
    message(FATAL_ERROR
            "SALTS_ROOT must identify the host Salts SDK used by salts-idlc")
  endif()
  file(TO_CMAKE_PATH "$ENV{SALTS_ROOT}" _idlc_salts_root)

  if(TARGET salts-idlc)
    set(${out_executable} "$<TARGET_FILE:salts-idlc>" PARENT_SCOPE)
    set(${out_dependency} "salts-idlc" PARENT_SCOPE)
    set(${out_runtime_root} "${CMAKE_BINARY_DIR}" PARENT_SCOPE)
    set(${out_salts_root} "${_idlc_salts_root}" PARENT_SCOPE)
    return()
  endif()

  set(_databind_hints)
  if(DEFINED SaltsUtils_DATABINDC_HINT AND
     NOT "${SaltsUtils_DATABINDC_HINT}" STREQUAL "")
    list(APPEND _databind_hints "${SaltsUtils_DATABINDC_HINT}")
  endif()

  unset(_idlc_program)
  unset(_idlc_program CACHE)
  if(_databind_hints)
    find_program(_idlc_program
      NAMES salts-idlc
      HINTS ${_databind_hints}
      NO_DEFAULT_PATH
      NO_CACHE)
    if(NOT _idlc_program)
      message(FATAL_ERROR
              "The installed SaltsUtils package is missing its matching host "
              "salts-idlc under: ${SaltsUtils_DATABINDC_HINT}. "
              "Set SaltsUtils_IDLC_EXECUTABLE explicitly only when a "
              "different qualified host tool is intentional.")
    endif()
  else()
    find_program(_idlc_program
      NAMES salts-idlc
      NO_CACHE)
    if(NOT _idlc_program)
      message(FATAL_ERROR
              "salts_idl_target() could not find host salts-idlc. "
              "Set SaltsUtils_IDLC_EXECUTABLE explicitly.")
    endif()
  endif()

  get_filename_component(_idlc_bin
    "${_idlc_program}" DIRECTORY)
  get_filename_component(_idlc_prefix
    "${_idlc_bin}" DIRECTORY)

  set(${out_executable} "${_idlc_program}" PARENT_SCOPE)
  set(${out_dependency} "${_idlc_program}" PARENT_SCOPE)
  set(${out_runtime_root} "${_idlc_prefix}" PARENT_SCOPE)
  set(${out_salts_root} "${_idlc_salts_root}" PARENT_SCOPE)
endfunction()

function(_saltsutils_idl_host_command
         out_command executable runtime_root salts_root)
  if(WIN32)
    set(_runtime_path
        "${runtime_root}/bin;${runtime_root}/lib;${salts_root}/bin;$ENV{PATH}")
    string(REPLACE ";" "\\;" _runtime_path "${_runtime_path}")
    set(${out_command}
        "${CMAKE_COMMAND};-E;env;PATH=${_runtime_path};${executable}"
        PARENT_SCOPE)
  elseif(APPLE)
    set(_runtime_path
        "${runtime_root}/bin:${runtime_root}/lib:${salts_root}/lib:$ENV{DYLD_LIBRARY_PATH}")
    set(${out_command}
        "${CMAKE_COMMAND};-E;env;DYLD_LIBRARY_PATH=${_runtime_path};${executable}"
        PARENT_SCOPE)
  else()
    set(_runtime_path
        "${runtime_root}/bin:${runtime_root}/lib:${salts_root}/lib:$ENV{LD_LIBRARY_PATH}")
    set(${out_command}
        "${CMAKE_COMMAND};-E;env;LD_LIBRARY_PATH=${_runtime_path};${executable}"
        PARENT_SCOPE)
  endif()
endfunction()

function(salts_idl_target)
  set(options)
  set(one_value_args
      TARGET
      IDL
      COMPONENT
      VERSION
      ARTIFACT_NAME
      PROJECTION_CONFIG)
  set(multi_value_args
      ARTIFACTS
      TRANSPORTS
      SOURCES
      LIBRARIES)
  cmake_parse_arguments(DB
    "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  foreach(required_arg IN ITEMS TARGET IDL)
    if(NOT DB_${required_arg})
      message(FATAL_ERROR
              "salts_idl_target() requires ${required_arg}")
    endif()
  endforeach()

  if(NOT DB_ARTIFACTS AND NOT DB_TRANSPORTS)
    message(FATAL_ERROR
            "salts_idl_target() requires at least one ARTIFACTS or TRANSPORTS entry")
  endif()

  if(NOT DB_ARTIFACT_NAME)
    set(DB_ARTIFACT_NAME "${DB_TARGET}")
  endif()
  if(NOT DB_ARTIFACT_NAME MATCHES "^[A-Za-z0-9_.-]+$" OR
     DB_ARTIFACT_NAME STREQUAL "." OR
     DB_ARTIFACT_NAME STREQUAL "..")
    message(FATAL_ERROR
            "salts_idl_target ARTIFACT_NAME is not a safe artifact basename: "
            "${DB_ARTIFACT_NAME}")
  endif()

  set(_normalized_artifacts)
  set(_normalized_transports)
  set(_has_plugin FALSE)
  set(_has_openapi FALSE)
  set(_has_http FALSE)
  set(_has_rpc FALSE)
  set(_has_socket FALSE)
  set(_has_flowmq FALSE)

  foreach(artifact IN LISTS DB_ARTIFACTS)
    string(TOUPPER "${artifact}" artifact_upper)
    if(artifact_upper STREQUAL "PLUGIN")
      set(_has_plugin TRUE)
    elseif(artifact_upper STREQUAL "OPENAPI")
      set(_has_openapi TRUE)
    else()
      message(FATAL_ERROR
              "salts_idl_target artifact is not publicly available yet: "
              "${artifact}")
    endif()
    list(APPEND _normalized_artifacts "${artifact_upper}")
  endforeach()

  foreach(transport IN LISTS DB_TRANSPORTS)
    string(TOUPPER "${transport}" transport_upper)
    if(transport_upper STREQUAL "HTTP")
      set(_has_http TRUE)
    elseif(transport_upper STREQUAL "RPC")
      set(_has_rpc TRUE)
    elseif(transport_upper STREQUAL "SOCKET")
      set(_has_socket TRUE)
    elseif(transport_upper STREQUAL "FLOWMQ")
      set(_has_flowmq TRUE)
    else()
      message(FATAL_ERROR
              "salts_idl_target transport is not publicly available yet: "
              "${transport}")
    endif()
    list(APPEND _normalized_transports "${transport_upper}")
  endforeach()

  if(_has_openapi AND NOT _has_http)
    message(FATAL_ERROR
            "salts_idl_target OPENAPI artifact requires HTTP transport")
  endif()

  set(_unique_artifacts ${_normalized_artifacts})
  list(REMOVE_DUPLICATES _unique_artifacts)
  list(LENGTH _normalized_artifacts _artifact_count)
  list(LENGTH _unique_artifacts _unique_artifact_count)
  if(NOT _artifact_count EQUAL _unique_artifact_count)
    message(FATAL_ERROR
            "salts_idl_target ARTIFACTS contains a duplicate selection")
  endif()

  set(_unique_transports ${_normalized_transports})
  list(REMOVE_DUPLICATES _unique_transports)
  list(LENGTH _normalized_transports _transport_count)
  list(LENGTH _unique_transports _unique_transport_count)
  if(NOT _transport_count EQUAL _unique_transport_count)
    message(FATAL_ERROR
            "salts_idl_target TRANSPORTS contains a duplicate selection")
  endif()

  foreach(reserved_target IN ITEMS
          "${DB_TARGET}"
          "${DB_TARGET}_idl_codegen")
    if(TARGET "${reserved_target}")
      message(FATAL_ERROR
              "salts_idl_target generated target already exists: "
              "${reserved_target}")
    endif()
  endforeach()
  if(_has_plugin)
    foreach(reserved_target IN ITEMS
            "${DB_TARGET}_plugin"
            "${DB_TARGET}_plugin_client")
      if(TARGET "${reserved_target}")
        message(FATAL_ERROR
                "salts_idl_target generated target already exists: "
                "${reserved_target}")
      endif()
    endforeach()
  endif()

  if(_has_plugin)
    foreach(plugin_arg IN ITEMS COMPONENT VERSION)
      if(NOT DB_${plugin_arg})
        message(FATAL_ERROR
                "salts_idl_target PLUGIN requires ${plugin_arg}")
      endif()
    endforeach()
    if(NOT DB_SOURCES AND NOT DB_LIBRARIES)
      message(FATAL_ERROR
              "salts_idl_target PLUGIN requires business implementation through "
              "SOURCES and/or LIBRARIES")
    endif()
    if(NOT DB_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
      message(FATAL_ERROR
              "salts_idl_target VERSION must be MAJOR.MINOR.PATCH")
    endif()
    string(REPLACE "." ";" _version_parts "${DB_VERSION}")
    foreach(_version_part IN LISTS _version_parts)
      string(REGEX REPLACE "^0+" "" _version_part_normalized "${_version_part}")
      if(_version_part_normalized STREQUAL "")
        set(_version_part_normalized "0")
      endif()
      string(LENGTH "${_version_part_normalized}" _version_part_length)
      if(_version_part_length GREATER 10 OR
         (_version_part_length EQUAL 10 AND
          _version_part_normalized STRGREATER "4294967295"))
        message(FATAL_ERROR
                "salts_idl_target VERSION components must fit uint32: "
                "${DB_VERSION}")
      endif()
    endforeach()
  else()
    if(DB_COMPONENT OR DB_VERSION)
      message(FATAL_ERROR
              "salts_idl_target COMPONENT/VERSION are only valid when PLUGIN "
              "is selected")
    endif()
    if(DB_SOURCES OR DB_LIBRARIES)
      message(FATAL_ERROR
              "salts_idl_target SOURCES/LIBRARIES are only consumed by PLUGIN")
    endif()
  endif()

  set(_projection_config)
  if(DB_PROJECTION_CONFIG)
    if(NOT _has_http AND NOT _has_rpc AND
       NOT _has_socket AND NOT _has_flowmq)
      message(FATAL_ERROR
              "salts_idl_target PROJECTION_CONFIG requires a configured transport")
    endif()
    get_filename_component(_projection_config
      "${DB_PROJECTION_CONFIG}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${_projection_config}")
      message(FATAL_ERROR
              "salts_idl_target PROJECTION_CONFIG does not exist: "
              "${_projection_config}")
    endif()
  endif()
  if(_has_socket AND NOT _projection_config)
    message(FATAL_ERROR
            "salts_idl_target SOCKET requires PROJECTION_CONFIG")
  endif()
  if(_has_flowmq AND NOT _projection_config)
    message(FATAL_ERROR
            "salts_idl_target FLOWMQ requires PROJECTION_CONFIG")
  endif()

  list(JOIN _normalized_artifacts "," _artifact_csv)
  string(TOLOWER "${_artifact_csv}" _artifact_csv)
  list(JOIN _normalized_transports "," _transport_csv)
  string(TOLOWER "${_transport_csv}" _transport_csv)

  get_filename_component(_idl
    "${DB_IDL}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_idl}")
    message(FATAL_ERROR
            "salts_idl_target IDL does not exist: ${_idl}")
  endif()

  _saltsutils_idl_resolve_compiler(
    _idlc
    _idlc_dependency
    _idlc_runtime_root
    _idlc_salts_root)
  _saltsutils_idl_host_command(
    _idlc_command
    "${_idlc}"
    "${_idlc_runtime_root}"
    "${_idlc_salts_root}")

  set(_generated_dir
      "${CMAKE_CURRENT_BINARY_DIR}/${DB_TARGET}.idl")
  set(_native_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}_native.h")
  set(_native_source
      "${_generated_dir}/${DB_ARTIFACT_NAME}_native.c")
  set(_plugin_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}.plugin.h")
  set(_plugin_source
      "${_generated_dir}/${DB_ARTIFACT_NAME}.plugin.c")
  set(_plugin_client_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}.plugin_client.h")
  set(_plugin_client_source
      "${_generated_dir}/${DB_ARTIFACT_NAME}.plugin_client.c")
  set(_openapi_output
      "${_generated_dir}/${DB_ARTIFACT_NAME}.openapi.json")
  set(_http_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}.http.h")
  set(_rpc_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}.rpc.h")
  set(_socket_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}.socket.h")
  set(_flowmq_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}.flowmq.h")

  set(_generated_outputs "${_native_header}")
  if(_has_socket OR _has_flowmq)
    list(APPEND _generated_outputs "${_native_source}")
  endif()
  if(_has_plugin)
    list(APPEND _generated_outputs
      "${_plugin_header}"
      "${_plugin_source}"
      "${_plugin_client_header}"
      "${_plugin_client_source}")
  endif()
  if(_has_openapi)
    list(APPEND _generated_outputs "${_openapi_output}")
  endif()
  if(_has_http)
    list(APPEND _generated_outputs "${_http_header}")
  endif()
  if(_has_rpc)
    list(APPEND _generated_outputs "${_rpc_header}")
  endif()
  if(_has_socket)
    list(APPEND _generated_outputs "${_socket_header}")
  endif()
  if(_has_flowmq)
    list(APPEND _generated_outputs "${_flowmq_header}")
  endif()

  set(_compiler_args
      "${_idl}"
      --lang c
      --output "${_native_header}"
      --artifact-name "${DB_ARTIFACT_NAME}")
  if(_normalized_artifacts)
    list(APPEND _compiler_args
      --artifacts "${_artifact_csv}")
  endif()
  if(_normalized_transports)
    list(APPEND _compiler_args
      --transports "${_transport_csv}")
  endif()
  if(_has_socket OR _has_flowmq)
    list(APPEND _compiler_args
      --source-output "${_native_source}")
  endif()
  if(_has_plugin)
    list(APPEND _compiler_args
      --component "${DB_COMPONENT}"
      --artifact-version "${DB_VERSION}")
  endif()
  if(_projection_config)
    list(APPEND _compiler_args
      --projection-config "${_projection_config}")
  endif()

  set(_generate_dependencies "${_idl}")
  if(_idlc_dependency)
    list(APPEND _generate_dependencies "${_idlc_dependency}")
  endif()
  if(_projection_config)
    list(APPEND _generate_dependencies "${_projection_config}")
  endif()

  add_custom_command(
    OUTPUT ${_generated_outputs}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
    COMMAND ${_idlc_command} ${_compiler_args}
    DEPENDS ${_generate_dependencies}
    VERBATIM
    COMMENT
      "Generating IDL ${DB_TARGET} artifacts=[${_artifact_csv}] transports=[${_transport_csv}]")

  set_source_files_properties(${_generated_outputs}
    PROPERTIES GENERATED TRUE)

  add_custom_target("${DB_TARGET}_idl_codegen"
    DEPENDS ${_generated_outputs})

  if(_has_socket OR _has_flowmq)
    add_library("${DB_TARGET}_native" STATIC
      "${_native_source}"
      "${_native_header}")
    add_dependencies("${DB_TARGET}_native"
      "${DB_TARGET}_idl_codegen")
    target_compile_features("${DB_TARGET}_native" PRIVATE c_std_11)
    target_include_directories("${DB_TARGET}_native" PUBLIC
      "${_generated_dir}")
    target_link_libraries("${DB_TARGET}_native" PUBLIC
      Salts::DataBind
      ${DB_LIBRARIES})
  endif()

  if(_has_plugin)
    add_library("${DB_TARGET}_plugin" SHARED
      "${_plugin_source}"
      "${_plugin_header}"
      "${_native_header}"
      ${DB_SOURCES})
    add_dependencies("${DB_TARGET}_plugin"
      "${DB_TARGET}_idl_codegen")
    target_compile_features("${DB_TARGET}_plugin" PRIVATE c_std_11)
    target_include_directories("${DB_TARGET}_plugin" PRIVATE
      "${_generated_dir}")
    target_link_libraries("${DB_TARGET}_plugin" PRIVATE
      Salts::PluginABI
      Salts::DataBind
      ${DB_LIBRARIES})
    set_target_properties("${DB_TARGET}_plugin" PROPERTIES
      PREFIX ""
      OUTPUT_NAME "${DB_ARTIFACT_NAME}")

    add_library("${DB_TARGET}_plugin_client" STATIC
      "${_plugin_client_source}"
      "${_plugin_client_header}"
      "${_native_header}")
    add_dependencies("${DB_TARGET}_plugin_client"
      "${DB_TARGET}_idl_codegen")
    target_compile_features("${DB_TARGET}_plugin_client" PRIVATE c_std_11)
    target_include_directories("${DB_TARGET}_plugin_client" PUBLIC
      "${_generated_dir}")
    target_link_libraries("${DB_TARGET}_plugin_client" PUBLIC
      Salts::Plugin
      Salts::DataBind)
  endif()

  add_custom_target("${DB_TARGET}")
  add_dependencies("${DB_TARGET}" "${DB_TARGET}_idl_codegen")
  if(_has_socket OR _has_flowmq)
    add_dependencies("${DB_TARGET}" "${DB_TARGET}_native")
  endif()
  if(_has_plugin)
    add_dependencies("${DB_TARGET}"
      "${DB_TARGET}_plugin"
      "${DB_TARGET}_plugin_client")
  endif()

  set_property(TARGET "${DB_TARGET}" PROPERTY
    DATABIND_GENERATED_DIR "${_generated_dir}")
  set_property(TARGET "${DB_TARGET}" PROPERTY
    DATABIND_ARTIFACTS "${_normalized_artifacts}")
  set_property(TARGET "${DB_TARGET}" PROPERTY
    DATABIND_TRANSPORTS "${_normalized_transports}")
  if(_projection_config)
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_PROJECTION_CONFIG "${_projection_config}")
  endif()

  if(_has_plugin)
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_COMPONENT "${DB_COMPONENT}")
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_PLUGIN_TARGET "${DB_TARGET}_plugin")
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_PLUGIN_CLIENT_TARGET "${DB_TARGET}_plugin_client")
    set(${DB_TARGET}_PLUGIN_TARGET
        "${DB_TARGET}_plugin" PARENT_SCOPE)
    set(${DB_TARGET}_PLUGIN_CLIENT_TARGET
        "${DB_TARGET}_plugin_client" PARENT_SCOPE)
  endif()
  if(_has_openapi)
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_OPENAPI_ARTIFACT "${_openapi_output}")
    set(${DB_TARGET}_OPENAPI_ARTIFACT
        "${_openapi_output}" PARENT_SCOPE)
  endif()
  if(_has_http)
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_HTTP_PROJECTION "${_http_header}")
    set(${DB_TARGET}_HTTP_PROJECTION
        "${_http_header}" PARENT_SCOPE)
  endif()
  if(_has_rpc)
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_RPC_PROJECTION "${_rpc_header}")
    set(${DB_TARGET}_RPC_PROJECTION
        "${_rpc_header}" PARENT_SCOPE)
  endif()
  if(_has_socket)
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_SOCKET_PROJECTION "${_socket_header}")
    set(${DB_TARGET}_SOCKET_PROJECTION
        "${_socket_header}" PARENT_SCOPE)
  endif()
  if(_has_flowmq)
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_FLOWMQ_PROJECTION "${_flowmq_header}")
    set(${DB_TARGET}_FLOWMQ_PROJECTION
        "${_flowmq_header}" PARENT_SCOPE)
  endif()
  if(_has_socket OR _has_flowmq)
    set_property(TARGET "${DB_TARGET}" PROPERTY
      DATABIND_NATIVE_TARGET "${DB_TARGET}_native")
    set(${DB_TARGET}_NATIVE_TARGET
        "${DB_TARGET}_native" PARENT_SCOPE)
  endif()

  set(${DB_TARGET}_GENERATED_DIR
      "${_generated_dir}" PARENT_SCOPE)
endfunction()

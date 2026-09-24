include_guard(GLOBAL)
include(CMakeParseArguments)

set(SaltsUtils_DATABINDC_EXECUTABLE ""
    CACHE FILEPATH
    "Host databindc executable used by databind_target()")

function(_saltsutils_databind_resolve_compiler out_command out_dependency)
  if(SaltsUtils_DATABINDC_EXECUTABLE)
    if(NOT EXISTS "${SaltsUtils_DATABINDC_EXECUTABLE}")
      message(FATAL_ERROR
              "SaltsUtils_DATABINDC_EXECUTABLE does not exist: "
              "${SaltsUtils_DATABINDC_EXECUTABLE}")
    endif()
    set(${out_command} "${SaltsUtils_DATABINDC_EXECUTABLE}" PARENT_SCOPE)
    set(${out_dependency} "" PARENT_SCOPE)
    return()
  endif()

  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
            "databind_target() requires a host databindc while cross-compiling. "
            "Set SaltsUtils_DATABINDC_EXECUTABLE to the host tool.")
  endif()

  if(TARGET databindc)
    set(${out_command} "$<TARGET_FILE:databindc>" PARENT_SCOPE)
    set(${out_dependency} "databindc" PARENT_SCOPE)
    return()
  endif()

  set(_databind_hints)
  if(DEFINED SaltsUtils_DATABINDC_HINT AND
     NOT "${SaltsUtils_DATABINDC_HINT}" STREQUAL "")
    list(APPEND _databind_hints "${SaltsUtils_DATABINDC_HINT}")
  endif()

  set(_databindc_program "")
  if(_databind_hints)
    find_program(_databindc_program
      NAMES databindc
      HINTS ${_databind_hints}
      NO_DEFAULT_PATH
      NO_CACHE)
  endif()
  if(NOT _databindc_program)
    find_program(_databindc_program
      NAMES databindc
      NO_CACHE)
  endif()
  if(NOT _databindc_program)
    message(FATAL_ERROR
            "databind_target() could not find host databindc. "
            "Set SaltsUtils_DATABINDC_EXECUTABLE explicitly.")
  endif()

  set(${out_command} "${_databindc_program}" PARENT_SCOPE)
  set(${out_dependency} "" PARENT_SCOPE)
endfunction()

function(databind_target)
  set(options)
  set(one_value_args
      TARGET
      IDL
      COMPONENT
      VERSION
      ARTIFACT_NAME)
  set(multi_value_args
      PROJECTIONS
      SOURCES
      LIBRARIES)
  cmake_parse_arguments(DB
    "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  foreach(required_arg IN ITEMS TARGET IDL COMPONENT VERSION)
    if(NOT DB_${required_arg})
      message(FATAL_ERROR
              "databind_target() requires ${required_arg}")
    endif()
  endforeach()

  if(NOT DB_PROJECTIONS)
    message(FATAL_ERROR
            "databind_target() requires at least one PROJECTIONS entry")
  endif()

  foreach(reserved_target IN ITEMS
          "${DB_TARGET}"
          "${DB_TARGET}_plugin"
          "${DB_TARGET}_databind_codegen")
    if(TARGET "${reserved_target}")
      message(FATAL_ERROR
              "databind_target generated target already exists: "
              "${reserved_target}")
    endif()
  endforeach()

  if(NOT DB_SOURCES AND NOT DB_LIBRARIES)
    message(FATAL_ERROR
            "databind_target PLUGIN requires business implementation through "
            "SOURCES and/or LIBRARIES")
  endif()

  if(NOT DB_ARTIFACT_NAME)
    set(DB_ARTIFACT_NAME "${DB_TARGET}")
  endif()

  if(NOT DB_ARTIFACT_NAME MATCHES "^[A-Za-z0-9_.-]+$" OR
     DB_ARTIFACT_NAME STREQUAL "." OR
     DB_ARTIFACT_NAME STREQUAL "..")
    message(FATAL_ERROR
            "databind_target ARTIFACT_NAME is not a safe artifact basename: "
            "${DB_ARTIFACT_NAME}")
  endif()

  if(NOT DB_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
            "databind_target VERSION must be MAJOR.MINOR.PATCH")
  endif()

  set(_normalized_projections)
  foreach(projection IN LISTS DB_PROJECTIONS)
    string(TOUPPER "${projection}" projection_upper)
    if(NOT projection_upper STREQUAL "PLUGIN")
      message(FATAL_ERROR
              "databind_target projection is not publicly available yet: "
              "${projection}")
    endif()
    list(APPEND _normalized_projections "${projection_upper}")
  endforeach()

  set(_unique_projections ${_normalized_projections})
  list(REMOVE_DUPLICATES _unique_projections)
  list(LENGTH _normalized_projections _projection_count)
  list(LENGTH _unique_projections _unique_projection_count)
  if(NOT _projection_count EQUAL _unique_projection_count)
    message(FATAL_ERROR
            "databind_target PROJECTIONS contains a duplicate backend")
  endif()

  list(JOIN _normalized_projections "," _projection_csv)
  string(TOLOWER "${_projection_csv}" _projection_csv)

  get_filename_component(_idl
    "${DB_IDL}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_idl}")
    message(FATAL_ERROR
            "databind_target IDL does not exist: ${_idl}")
  endif()

  _saltsutils_databind_resolve_compiler(
    _databindc _databindc_dependency)

  set(_generated_dir
      "${CMAKE_CURRENT_BINARY_DIR}/${DB_TARGET}.databind")
  set(_native_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}_native.h")
  set(_plugin_header
      "${_generated_dir}/${DB_ARTIFACT_NAME}.plugin.h")
  set(_plugin_source
      "${_generated_dir}/${DB_ARTIFACT_NAME}.plugin.c")

  set(_generate_dependencies "${_idl}")
  if(_databindc_dependency)
    list(APPEND _generate_dependencies "${_databindc_dependency}")
  endif()

  add_custom_command(
    OUTPUT
      "${_native_header}"
      "${_plugin_header}"
      "${_plugin_source}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
    COMMAND "${_databindc}"
      "${_idl}"
      --lang c
      --output "${_native_header}"
      --projections "${_projection_csv}"
      --component "${DB_COMPONENT}"
      --artifact-name "${DB_ARTIFACT_NAME}"
      --artifact-version "${DB_VERSION}"
    DEPENDS ${_generate_dependencies}
    VERBATIM
    COMMENT
      "Generating DataBind ${DB_TARGET} artifacts from ${DB_COMPONENT}")

  set_source_files_properties(
    "${_native_header}"
    "${_plugin_header}"
    "${_plugin_source}"
    PROPERTIES GENERATED TRUE)

  add_custom_target("${DB_TARGET}_databind_codegen"
    DEPENDS
      "${_native_header}"
      "${_plugin_header}"
      "${_plugin_source}")

  add_library("${DB_TARGET}_plugin" SHARED
    "${_plugin_source}"
    "${_plugin_header}"
    "${_native_header}"
    ${DB_SOURCES})
  add_dependencies("${DB_TARGET}_plugin"
    "${DB_TARGET}_databind_codegen")
  target_compile_features("${DB_TARGET}_plugin" PRIVATE c_std_11)
  target_include_directories("${DB_TARGET}_plugin" PRIVATE
    "${_generated_dir}")
  target_link_libraries("${DB_TARGET}_plugin" PRIVATE
    Salts::PluginABI
    Salts::DataBindSchema
    ${DB_LIBRARIES})
  set_target_properties("${DB_TARGET}_plugin" PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${DB_ARTIFACT_NAME}")

  add_custom_target("${DB_TARGET}")
  add_dependencies("${DB_TARGET}" "${DB_TARGET}_plugin")

  set_property(TARGET "${DB_TARGET}" PROPERTY
    DATABIND_COMPONENT "${DB_COMPONENT}")
  set_property(TARGET "${DB_TARGET}" PROPERTY
    DATABIND_GENERATED_DIR "${_generated_dir}")
  set_property(TARGET "${DB_TARGET}" PROPERTY
    DATABIND_PLUGIN_TARGET "${DB_TARGET}_plugin")

  set(${DB_TARGET}_PLUGIN_TARGET
      "${DB_TARGET}_plugin" PARENT_SCOPE)
  set(${DB_TARGET}_GENERATED_DIR
      "${_generated_dir}" PARENT_SCOPE)
endfunction()

include_guard(GLOBAL)
include(CMakeParseArguments)

set(SaltsUtils_DATABINDC_EXECUTABLE ""
    CACHE FILEPATH
    "Host databindc executable used by databind_target()")
set(SaltsUtils_DATABINDC_HOST_SALTS_ROOT ""
    CACHE PATH
    "Host Salts SDK used to run databindc while cross-compiling")

function(_saltsutils_databind_resolve_compiler
         out_executable out_dependency out_runtime_root out_salts_root)
  if(SaltsUtils_DATABINDC_EXECUTABLE)
    if(NOT EXISTS "${SaltsUtils_DATABINDC_EXECUTABLE}")
      message(FATAL_ERROR
              "SaltsUtils_DATABINDC_EXECUTABLE does not exist: "
              "${SaltsUtils_DATABINDC_EXECUTABLE}")
    endif()

    get_filename_component(_databindc_bin
      "${SaltsUtils_DATABINDC_EXECUTABLE}" DIRECTORY)
    get_filename_component(_databindc_prefix
      "${_databindc_bin}" DIRECTORY)

    if(CMAKE_CROSSCOMPILING)
      if(NOT SaltsUtils_DATABINDC_HOST_SALTS_ROOT)
        message(FATAL_ERROR
                "Cross-compiling databind_target() requires "
                "SaltsUtils_DATABINDC_HOST_SALTS_ROOT for the host compiler "
                "runtime closure.")
      endif()
      set(_databindc_salts_root
          "${SaltsUtils_DATABINDC_HOST_SALTS_ROOT}")
    else()
      if(NOT DEFINED ENV{SALTS_ROOT} OR "$ENV{SALTS_ROOT}" STREQUAL "")
        message(FATAL_ERROR
                "SALTS_ROOT must identify the host Salts SDK used by databindc")
      endif()
      file(TO_CMAKE_PATH "$ENV{SALTS_ROOT}" _databindc_salts_root)
    endif()

    set(${out_executable}
        "${SaltsUtils_DATABINDC_EXECUTABLE}" PARENT_SCOPE)
    set(${out_dependency}
        "${SaltsUtils_DATABINDC_EXECUTABLE}" PARENT_SCOPE)
    set(${out_runtime_root}
        "${_databindc_prefix}" PARENT_SCOPE)
    set(${out_salts_root}
        "${_databindc_salts_root}" PARENT_SCOPE)
    return()
  endif()

  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
            "databind_target() requires a host databindc while cross-compiling. "
            "Set SaltsUtils_DATABINDC_EXECUTABLE and "
            "SaltsUtils_DATABINDC_HOST_SALTS_ROOT.")
  endif()

  if(NOT DEFINED ENV{SALTS_ROOT} OR "$ENV{SALTS_ROOT}" STREQUAL "")
    message(FATAL_ERROR
            "SALTS_ROOT must identify the host Salts SDK used by databindc")
  endif()
  file(TO_CMAKE_PATH "$ENV{SALTS_ROOT}" _databindc_salts_root)

  if(TARGET databindc)
    set(${out_executable} "$<TARGET_FILE:databindc>" PARENT_SCOPE)
    set(${out_dependency} "databindc" PARENT_SCOPE)
    set(${out_runtime_root} "${CMAKE_BINARY_DIR}" PARENT_SCOPE)
    set(${out_salts_root} "${_databindc_salts_root}" PARENT_SCOPE)
    return()
  endif()

  set(_databind_hints)
  if(DEFINED SaltsUtils_DATABINDC_HINT AND
     NOT "${SaltsUtils_DATABINDC_HINT}" STREQUAL "")
    list(APPEND _databind_hints "${SaltsUtils_DATABINDC_HINT}")
  endif()

  unset(_databindc_program)
  unset(_databindc_program CACHE)
  if(_databind_hints)
    find_program(_databindc_program
      NAMES databindc
      HINTS ${_databind_hints}
      NO_DEFAULT_PATH
      NO_CACHE)
    if(NOT _databindc_program)
      message(FATAL_ERROR
              "The installed SaltsUtils package is missing its matching host "
              "databindc under: ${SaltsUtils_DATABINDC_HINT}. "
              "Set SaltsUtils_DATABINDC_EXECUTABLE explicitly only when a "
              "different qualified host tool is intentional.")
    endif()
  else()
    find_program(_databindc_program
      NAMES databindc
      NO_CACHE)
    if(NOT _databindc_program)
      message(FATAL_ERROR
              "databind_target() could not find host databindc. "
              "Set SaltsUtils_DATABINDC_EXECUTABLE explicitly.")
    endif()
  endif()

  get_filename_component(_databindc_bin
    "${_databindc_program}" DIRECTORY)
  get_filename_component(_databindc_prefix
    "${_databindc_bin}" DIRECTORY)

  set(${out_executable} "${_databindc_program}" PARENT_SCOPE)
  set(${out_dependency} "${_databindc_program}" PARENT_SCOPE)
  set(${out_runtime_root} "${_databindc_prefix}" PARENT_SCOPE)
  set(${out_salts_root} "${_databindc_salts_root}" PARENT_SCOPE)
endfunction()

function(_saltsutils_databind_host_command
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
              "databind_target VERSION components must fit uint32: "
              "${DB_VERSION}")
    endif()
  endforeach()

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
    _databindc
    _databindc_dependency
    _databindc_runtime_root
    _databindc_salts_root)
  _saltsutils_databind_host_command(
    _databindc_command
    "${_databindc}"
    "${_databindc_runtime_root}"
    "${_databindc_salts_root}")

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
    COMMAND ${_databindc_command}
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
    Salts::DataBindGeneratedABI
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

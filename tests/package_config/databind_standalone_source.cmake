cmake_minimum_required(VERSION 3.27)

foreach(required_var IN ITEMS
        PARENT_SOURCE_ROOT
        PARENT_BUILD_DIR
        PARTITION_ROOT
        TEST_ROOT
        SALTS_ROOT
        CMAKE_GENERATOR_NAME)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR "${required_var} is required")
  endif()
endforeach()

if(NOT DEFINED ENV{RE2C_ROOT} OR "$ENV{RE2C_ROOT}" STREQUAL "")
  message(FATAL_ERROR "RE2C_ROOT is required for the standalone proof")
endif()
file(TO_CMAKE_PATH "$ENV{RE2C_ROOT}" re2c_root)
find_program(standalone_re2c
  NAMES re2c
  PATHS "${re2c_root}/bin"
  NO_DEFAULT_PATH
  REQUIRED)

set(standalone_lempar "${PARENT_SOURCE_ROOT}/tools/lemon/lempar.c")
if(NOT EXISTS "${standalone_lempar}")
  message(FATAL_ERROR "parent Lemon template is missing: ${standalone_lempar}")
endif()

file(GLOB standalone_lemon_candidates
  "${PARENT_BUILD_DIR}/bin/lemon"
  "${PARENT_BUILD_DIR}/bin/lemon.exe")
list(LENGTH standalone_lemon_candidates standalone_lemon_count)
if(NOT standalone_lemon_count EQUAL 1)
  message(FATAL_ERROR
          "standalone proof requires exactly one built host Lemon executable: ${standalone_lemon_candidates}")
endif()
list(GET standalone_lemon_candidates 0 standalone_lemon)

set(salts_utils_root "${PARTITION_ROOT}/salts-utils")
if(NOT EXISTS "${salts_utils_root}/lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake")
  message(FATAL_ERROR
          "standalone proof requires the SaltsUtils-only partition root")
endif()
if(EXISTS "${salts_utils_root}/lib/cmake/DataBind/DataBindConfig.cmake")
  message(FATAL_ERROR
          "standalone proof dependency root unexpectedly owns DataBind")
endif()

set(standalone_build "${TEST_ROOT}/build")
set(standalone_install "${TEST_ROOT}/install")
set(consumer_build "${TEST_ROOT}/consumer")
file(REMOVE_RECURSE
     "${standalone_build}" "${standalone_install}" "${consumer_build}")

set(configure_command
    "${CMAKE_COMMAND}" -E env
    "SALTS_ROOT=${SALTS_ROOT}"
    "SALTS_UTILS_ROOT=${salts_utils_root}"
    "${CMAKE_COMMAND}" --fresh
    -S "${PARENT_SOURCE_ROOT}/tbe"
    -B "${standalone_build}"
    -G "${CMAKE_GENERATOR_NAME}"
    "-DDATABIND_RE2C_EXECUTABLE=${standalone_re2c}"
    "-DDATABIND_LEMPAR=${standalone_lempar}"
    "-DDATABIND_HOST_LEMON_EXECUTABLE=${standalone_lemon}"
    -DBUILD_TESTING=OFF
    -DBUILD_BENCHMARKS=OFF)

if(DEFINED CMAKE_TOOLCHAIN_FILE_VALUE AND
   NOT "${CMAKE_TOOLCHAIN_FILE_VALUE}" STREQUAL "")
  list(APPEND configure_command
       "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE_VALUE}")
endif()
if(DEFINED CMAKE_PREFIX_PATH_VALUE AND
   NOT "${CMAKE_PREFIX_PATH_VALUE}" STREQUAL "")
  list(APPEND configure_command
       "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH_VALUE}")
endif()
if(DEFINED CMAKE_BUILD_TYPE_VALUE AND
   NOT "${CMAKE_BUILD_TYPE_VALUE}" STREQUAL "")
  list(APPEND configure_command
       "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE_VALUE}")
endif()

execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "standalone DataBind configure failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${standalone_build}"
          --target data_bind data_bind_cmeta data_bind_cflow tbe_compiler
          --parallel 2
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "standalone DataBind production build failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${standalone_build}"
          --prefix "${standalone_install}" --component DataBind
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "standalone DataBind install failed")
endif()

file(GLOB_RECURSE databind_configs
  "${standalone_install}/*/cmake/DataBind/DataBindConfig.cmake")
list(LENGTH databind_configs databind_config_count)
if(NOT databind_config_count EQUAL 1)
  message(FATAL_ERROR
          "standalone install must contain exactly one DataBindConfig.cmake: ${databind_configs}")
endif()
list(GET databind_configs 0 databind_config)
get_filename_component(databind_package_dir "${databind_config}" DIRECTORY)

file(GLOB_RECURSE salts_utils_configs
  "${standalone_install}/*/cmake/SaltsUtils/SaltsUtilsConfig.cmake")
if(NOT salts_utils_configs STREQUAL "")
  message(FATAL_ERROR
          "standalone DataBind install unexpectedly contains SaltsUtilsConfig.cmake")
endif()

if(NOT EXISTS "${standalone_install}/include/data_bind.h")
  message(FATAL_ERROR "standalone DataBind install is missing data_bind.h")
endif()
file(GLOB compiler_candidates
  "${standalone_install}/bin/tbe_compiler"
  "${standalone_install}/bin/tbe_compiler.exe")
if(compiler_candidates STREQUAL "")
  message(FATAL_ERROR "standalone DataBind install is missing tbe_compiler")
endif()
if(NOT IS_DIRECTORY "${standalone_install}/bin/templates")
  message(FATAL_ERROR "standalone DataBind install is missing compiler templates")
endif()

set(consumer_command
    "${CMAKE_COMMAND}" -E env
    --unset=SALTS_UTILS_ROOT
    "SALTS_ROOT=${SALTS_ROOT}"
    "${CMAKE_COMMAND}" --fresh
    -S "${PARENT_SOURCE_ROOT}/tbe/tests/package_config/databind_repeated_find"
    -B "${consumer_build}"
    -G "${CMAKE_GENERATOR_NAME}"
    "-DDATABIND_PACKAGE_DIR=${databind_package_dir}")

if(DEFINED CMAKE_TOOLCHAIN_FILE_VALUE AND
   NOT "${CMAKE_TOOLCHAIN_FILE_VALUE}" STREQUAL "")
  list(APPEND consumer_command
       "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE_VALUE}")
endif()
if(DEFINED CMAKE_PREFIX_PATH_VALUE AND
   NOT "${CMAKE_PREFIX_PATH_VALUE}" STREQUAL "")
  list(APPEND consumer_command
       "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH_VALUE}")
endif()

execute_process(
  COMMAND ${consumer_command}
  RESULT_VARIABLE consumer_result)
if(NOT consumer_result EQUAL 0)
  message(FATAL_ERROR
          "installed standalone DataBind Base consumer failed without SALTS_UTILS_ROOT")
endif()

message(STATUS
        "Standalone DataBind source/build/install/Base-consumer contract passed")

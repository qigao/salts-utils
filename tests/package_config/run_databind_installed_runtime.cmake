cmake_minimum_required(VERSION 3.27)

foreach(required_var IN ITEMS
    PROJECT_BINARY_DIR
    PROJECT_SOURCE_DIR
    SALTS_ROOT_PATH
    TEST_GENERATOR)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_var}")
  endif()
endforeach()

set(stage_dir "${PROJECT_BINARY_DIR}/databind-installed-runtime-stage")
set(consumer_build "${PROJECT_BINARY_DIR}/databind-installed-runtime-consumer")
file(REMOVE_RECURSE "${stage_dir}" "${consumer_build}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
          --prefix "${stage_dir}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR
    "DataBind staged install failed\n${install_output}\n${install_error}")
endif()

if(NOT EXISTS "${stage_dir}/lib/cmake/DataBind/DataBindConfig.cmake")
  message(FATAL_ERROR "Staged install is missing DataBindConfig.cmake")
endif()
if(NOT EXISTS "${stage_dir}/lib/cmake/DataBind/DataBindTargets.cmake")
  message(FATAL_ERROR "Staged install is missing DataBindTargets.cmake")
endif()
if(NOT EXISTS "${stage_dir}/lib/cmake/DataBind/DataBindAdapterTargets.cmake")
  message(FATAL_ERROR "Staged install is missing DataBindAdapterTargets.cmake")
endif()

set(configure_command
  "${CMAKE_COMMAND}" -E env
  "SALTS_ROOT=${SALTS_ROOT_PATH}"
  "SALTS_UTILS_ROOT="
  "DATABIND_ROOT=${stage_dir}"
  "${CMAKE_COMMAND}" --fresh
  -S "${PROJECT_SOURCE_DIR}/tests/package_config/databind_runtime"
  -B "${consumer_build}"
  -G "${TEST_GENERATOR}")

if(DEFINED CMAKE_PREFIX_PATH_VALUE AND
   NOT "${CMAKE_PREFIX_PATH_VALUE}" STREQUAL "")
  list(APPEND configure_command
       "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH_VALUE}")
endif()
if(DEFINED CMAKE_TOOLCHAIN_FILE_VALUE AND
   NOT "${CMAKE_TOOLCHAIN_FILE_VALUE}" STREQUAL "")
  list(APPEND configure_command
       "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE_VALUE}")
endif()
if(DEFINED TEST_BUILD_TYPE AND NOT "${TEST_BUILD_TYPE}" STREQUAL "")
  list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${TEST_BUILD_TYPE}")
endif()

execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "Installed Runtime-only DataBind consumer configure failed\n"
    "${configure_output}\n${configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR
    "Installed Runtime-only DataBind consumer build failed\n"
    "${build_output}\n${build_error}")
endif()

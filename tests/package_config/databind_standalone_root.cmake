cmake_minimum_required(VERSION 3.27)

foreach(required_var IN ITEMS
        DATABIND_SOURCE_DIR
        STANDALONE_BUILD_DIR
        SALTS_ROOT
        SALTS_UTILS_ROOT
        DATABIND_RE2C_EXECUTABLE
        DATABIND_LEMPAR
        DATABIND_HOST_LEMON_EXECUTABLE
        CMAKE_GENERATOR_NAME)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR "${required_var} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${STANDALONE_BUILD_DIR}")

set(configure_command
  "${CMAKE_COMMAND}" --fresh
  -S "${DATABIND_SOURCE_DIR}"
  -B "${STANDALONE_BUILD_DIR}"
  -G "${CMAKE_GENERATOR_NAME}"
  "-DCMAKE_BUILD_TYPE=Debug"
  "-DBUILD_TESTING=OFF"
  "-DBUILD_BENCHMARKS=OFF"
  "-DDATABIND_RE2C_EXECUTABLE=${DATABIND_RE2C_EXECUTABLE}"
  "-DDATABIND_LEMPAR=${DATABIND_LEMPAR}"
  "-DDATABIND_HOST_LEMON_EXECUTABLE=${DATABIND_HOST_LEMON_EXECUTABLE}")

if(DEFINED CMAKE_TOOLCHAIN_FILE_PATH AND
   NOT "${CMAKE_TOOLCHAIN_FILE_PATH}" STREQUAL "")
  list(APPEND configure_command
       "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE_PATH}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "SALTS_ROOT=${SALTS_ROOT}"
          "SALTS_UTILS_ROOT=${SALTS_UTILS_ROOT}"
          ${configure_command}
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Standalone DataBind configure failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${STANDALONE_BUILD_DIR}"
          --target tbe_schema --parallel 2
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Standalone DataBind schema build failed")
endif()

if(NOT EXISTS "${STANDALONE_BUILD_DIR}/DataBindConfig.cmake")
  message(FATAL_ERROR "Standalone DataBind build did not generate DataBindConfig.cmake")
endif()
if(NOT EXISTS "${STANDALONE_BUILD_DIR}/DataBindTargets.cmake")
  message(FATAL_ERROR "Standalone DataBind build did not generate DataBindTargets.cmake")
endif()

message(STATUS "Standalone DataBind project-root contract passed")

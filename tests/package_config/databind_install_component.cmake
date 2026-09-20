cmake_minimum_required(VERSION 3.27)

foreach(required_var IN ITEMS BUILD_DIR TEST_ROOT)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR "${required_var} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
set(salts_utils_root "${TEST_ROOT}/salts-utils")
set(databind_root "${TEST_ROOT}/databind")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --parallel 2
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Build tree is not install-ready")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
          --prefix "${salts_utils_root}" --component Unspecified
  RESULT_VARIABLE salts_utils_install_result)
if(NOT salts_utils_install_result EQUAL 0)
  message(FATAL_ERROR "SaltsUtils component install failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
          --prefix "${databind_root}" --component DataBind
  RESULT_VARIABLE databind_install_result)
if(NOT databind_install_result EQUAL 0)
  message(FATAL_ERROR "DataBind component install failed")
endif()

if(NOT EXISTS "${salts_utils_root}/lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake")
  message(FATAL_ERROR "Unspecified install is missing SaltsUtilsConfig.cmake")
endif()
if(EXISTS "${salts_utils_root}/lib/cmake/DataBind/DataBindConfig.cmake")
  message(FATAL_ERROR "SaltsUtils root still contains DataBind package ownership")
endif()

if(NOT EXISTS "${databind_root}/lib/cmake/DataBind/DataBindConfig.cmake")
  message(FATAL_ERROR "DataBind install is missing DataBindConfig.cmake")
endif()
if(EXISTS "${databind_root}/lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake")
  message(FATAL_ERROR "DataBind root unexpectedly contains SaltsUtils package ownership")
endif()
if(NOT EXISTS "${databind_root}/include/data_bind.h")
  message(FATAL_ERROR "DataBind install is missing data_bind.h")
endif()
if(NOT EXISTS "${databind_root}/include/tbe_schema.h")
  file(GLOB schema_headers "${databind_root}/include/*schema*.h")
  if(schema_headers STREQUAL "")
    message(FATAL_ERROR "DataBind install is missing schema public headers")
  endif()
endif()
file(GLOB compiler_candidates
  "${databind_root}/bin/tbe_compiler"
  "${databind_root}/bin/tbe_compiler.exe")
if(compiler_candidates STREQUAL "")
  message(FATAL_ERROR "DataBind install is missing tbe_compiler")
endif()
if(NOT IS_DIRECTORY "${databind_root}/bin/templates")
  message(FATAL_ERROR "DataBind install is missing compiler templates")
endif()

message(STATUS "DataBind/SaltsUtils install component partition passed")

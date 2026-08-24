cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS
    TP_MAIN_BINARY_DIR TP_CONSUMER_SOURCE_DIR
    TP_TURBOUTILS_DIR TP_TURBOUTILS_RUNTIME_DIR
    TP_INSTALL_CMAKEDIR TP_INSTALL_LIBDIR
    TP_GENERATOR TP_CONFIG)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()
if(NOT IS_DIRECTORY "${TP_TURBOUTILS_RUNTIME_DIR}")
  message(FATAL_ERROR
    "TurboUtils runtime directory does not exist: ${TP_TURBOUTILS_RUNTIME_DIR}")
endif()

if(DEFINED TP_TEST_ROOT)
  message(FATAL_ERROR
    "Refusing unsafe install-consumer sandbox: TP_TEST_ROOT cannot override the fixed directory")
endif()

file(REAL_PATH "${TP_MAIN_BINARY_DIR}" main_binary_dir)
set(test_root "${main_binary_dir}/tbe/tbe_cbind/install_consumer")
include("${CMAKE_CURRENT_LIST_DIR}/install_consumer_path_guard.cmake")
tbe_cbind_reset_install_consumer_sandbox(
  "${main_binary_dir}" "${test_root}" FALSE)
set(install_prefix "${test_root}/prefix")
set(consumer_binary_dir "${test_root}/build")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${main_binary_dir}"
          --config "${TP_CONFIG}"
  RESULT_VARIABLE main_build_result
  OUTPUT_VARIABLE main_build_output
  ERROR_VARIABLE main_build_error)
if(NOT main_build_result EQUAL 0)
  message(FATAL_ERROR
    "Main build failed before install consumer:\n${main_build_output}\n${main_build_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${main_binary_dir}"
          --prefix "${install_prefix}" --config "${TP_CONFIG}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
file(WRITE "${test_root}/install.log" "${install_output}\n${install_error}")
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR
    "Isolated TurboParser install failed:\n${install_output}\n${install_error}")
endif()

set(configure_command
  "${CMAKE_COMMAND}"
  -S "${TP_CONSUMER_SOURCE_DIR}"
  -B "${consumer_binary_dir}"
  -G "${TP_GENERATOR}"
  "-DTurboParser_DIR=${install_prefix}/${TP_INSTALL_CMAKEDIR}"
  "-DTurboUtils_DIR=${TP_TURBOUTILS_DIR}"
  "-DCMAKE_BUILD_TYPE=${TP_CONFIG}")
if(DEFINED TP_DATABIND_PATH_ONLY AND TP_DATABIND_PATH_ONLY)
  list(APPEND configure_command -DTBE_CBIND_TEST_DATABIND_PATH_ONLY=ON)
endif()
if(DEFINED TP_PREFIX_PATH AND NOT "${TP_PREFIX_PATH}" STREQUAL "")
  list(APPEND configure_command "-DCMAKE_PREFIX_PATH=${TP_PREFIX_PATH}")
endif()
execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
file(WRITE "${test_root}/configure.log"
  "${configure_output}\n${configure_error}")
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "Installed consumer configure failed:\n${configure_output}\n${configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DTP_EVIDENCE_DIR=${consumer_binary_dir}/tbe-cbind-dependency-evidence
    -DTP_CONFIG=${TP_CONFIG}
    -DTP_OUTPUT_FILE=${consumer_binary_dir}/tbe-cbind-link-closure.txt
    -P "${CMAKE_CURRENT_LIST_DIR}/verify_install_consumer_dependencies.cmake"
  RESULT_VARIABLE dependency_result
  OUTPUT_VARIABLE dependency_output
  ERROR_VARIABLE dependency_error)
file(WRITE "${test_root}/dependency-evidence.log"
  "${dependency_output}\n${dependency_error}")
if(NOT dependency_result EQUAL 0)
  message(FATAL_ERROR
    "Installed consumer dependency evidence failed:\n"
    "${dependency_output}\n${dependency_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_binary_dir}"
          --config "${TP_CONFIG}" --verbose
  RESULT_VARIABLE consumer_build_result
  OUTPUT_VARIABLE consumer_build_output
  ERROR_VARIABLE consumer_build_error)
set(consumer_link_log "${consumer_build_output}\n${consumer_build_error}")
file(WRITE "${test_root}/consumer-build.log" "${consumer_link_log}")
if(NOT consumer_build_result EQUAL 0)
  message(FATAL_ERROR
    "Installed consumer build failed:\n${consumer_link_log}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DTP_BUILD_DIR=${consumer_binary_dir}
    -DTP_LINK_LOG=${test_root}/consumer-build.log
    -DTP_LINK_TARGET=tbe_cbind_install_consumer
    -DTP_OUTPUT_FILE=${test_root}/expanded-link-evidence.txt
    -DTP_DEPENDENCY_OUTPUT_FILE=${test_root}/dependency-link-tokens.txt
    -P "${CMAKE_CURRENT_LIST_DIR}/verify_install_consumer_link.cmake"
  RESULT_VARIABLE link_evidence_result
  OUTPUT_VARIABLE link_evidence_output
  ERROR_VARIABLE link_evidence_error)
if(NOT link_evidence_result EQUAL 0)
  message(FATAL_ERROR
    "Installed consumer link evidence failed:\n"
    "${link_evidence_output}\n${link_evidence_error}")
endif()
if(DEFINED TP_DATABIND_PATH_ONLY AND TP_DATABIND_PATH_ONLY)
  file(READ "${test_root}/expanded-link-evidence.txt" raw_link_evidence)
  file(READ "${test_root}/dependency-link-tokens.txt" dependency_tokens)
  if(NOT raw_link_evidence MATCHES "DataBindCheckoutBuildOutput")
    message(FATAL_ERROR
      "The path-only DataBind control did not reach the actual link command")
  endif()
  string(TOLOWER "${dependency_tokens}" dependency_tokens_lower)
  if("${dependency_tokens}" STREQUAL "" OR
     dependency_tokens_lower MATCHES "databind|data_bind")
    message(FATAL_ERROR
      "Path-only control contaminated parsed dependency tokens: "
      "${dependency_tokens}")
  endif()
endif()

set(consumer_location_file
  "${consumer_binary_dir}/tbe-cbind-consumer-location-${TP_CONFIG}.txt")
if(NOT EXISTS "${consumer_location_file}")
  message(FATAL_ERROR
    "Installed consumer target location evidence was not produced: "
    "${consumer_location_file}")
endif()
file(READ "${consumer_location_file}" consumer_executable)
string(STRIP "${consumer_executable}" consumer_executable)
if(NOT EXISTS "${consumer_executable}")
  message(FATAL_ERROR
    "Installed consumer executable was not produced: ${consumer_executable}")
endif()

if(WIN32)
  set(runtime_path
    "${install_prefix}/bin;${TP_TURBOUTILS_RUNTIME_DIR}")
  if(DEFINED ENV{PATH} AND NOT "$ENV{PATH}" STREQUAL "")
    string(APPEND runtime_path ";$ENV{PATH}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "PATH=${runtime_path}"
      "${consumer_executable}"
    RESULT_VARIABLE consumer_run_result
    OUTPUT_VARIABLE consumer_run_output
    ERROR_VARIABLE consumer_run_error)
elseif(APPLE)
  set(runtime_path "${install_prefix}/bin")
  if(DEFINED ENV{PATH} AND NOT "$ENV{PATH}" STREQUAL "")
    string(APPEND runtime_path ":$ENV{PATH}")
  endif()
  set(runtime_library_path
    "${install_prefix}/${TP_INSTALL_LIBDIR}:${TP_TURBOUTILS_RUNTIME_DIR}")
  if(DEFINED ENV{DYLD_LIBRARY_PATH} AND
     NOT "$ENV{DYLD_LIBRARY_PATH}" STREQUAL "")
    string(APPEND runtime_library_path ":$ENV{DYLD_LIBRARY_PATH}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "PATH=${runtime_path}"
      "DYLD_LIBRARY_PATH=${runtime_library_path}"
      "${consumer_executable}"
    RESULT_VARIABLE consumer_run_result
    OUTPUT_VARIABLE consumer_run_output
    ERROR_VARIABLE consumer_run_error)
else()
  set(runtime_path "${install_prefix}/bin")
  if(DEFINED ENV{PATH} AND NOT "$ENV{PATH}" STREQUAL "")
    string(APPEND runtime_path ":$ENV{PATH}")
  endif()
  set(runtime_library_path
    "${install_prefix}/${TP_INSTALL_LIBDIR}:${TP_TURBOUTILS_RUNTIME_DIR}")
  if(DEFINED ENV{LD_LIBRARY_PATH} AND
     NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
    string(APPEND runtime_library_path ":$ENV{LD_LIBRARY_PATH}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "PATH=${runtime_path}"
      "LD_LIBRARY_PATH=${runtime_library_path}"
      "${consumer_executable}"
    RESULT_VARIABLE consumer_run_result
    OUTPUT_VARIABLE consumer_run_output
    ERROR_VARIABLE consumer_run_error)
endif()
file(WRITE "${test_root}/consumer-run.log"
  "${consumer_run_output}\n${consumer_run_error}")
if(NOT consumer_run_result EQUAL 0)
  message(FATAL_ERROR
    "Installed consumer exited with ${consumer_run_result}:\n"
    "${consumer_run_output}\n${consumer_run_error}")
endif()

cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS
    TP_MAIN_BINARY_DIR TP_TEST_ROOT TP_CONSUMER_SOURCE_DIR
    TP_TURBOUTILS_DIR TP_INSTALL_CMAKEDIR TP_GENERATOR TP_CONFIG)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

file(TO_CMAKE_PATH "${TP_MAIN_BINARY_DIR}" main_binary_dir)
file(TO_CMAKE_PATH "${TP_TEST_ROOT}" test_root)
string(TOLOWER "${main_binary_dir}/" main_binary_prefix_lower)
string(TOLOWER "${test_root}/" test_root_lower)
string(FIND "${test_root_lower}" "${main_binary_prefix_lower}" root_position)
if(NOT root_position EQUAL 0 OR test_root STREQUAL main_binary_dir)
  message(FATAL_ERROR
    "Refusing to reset install-consumer paths outside the main build tree: ${test_root}")
endif()

file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")
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
string(TOLOWER "${consumer_link_log}" consumer_link_log_lower)
if(consumer_link_log_lower MATCHES "databind|data_bind")
  message(FATAL_ERROR
    "Installed consumer link command contains a forbidden DataBind dependency")
endif()

set(consumer_executable "${consumer_binary_dir}/tbe_cbind_install_consumer")
if(WIN32)
  string(APPEND consumer_executable ".exe")
endif()
if(NOT EXISTS "${consumer_executable}")
  set(consumer_executable
    "${consumer_binary_dir}/${TP_CONFIG}/tbe_cbind_install_consumer")
  if(WIN32)
    string(APPEND consumer_executable ".exe")
  endif()
endif()
if(NOT EXISTS "${consumer_executable}")
  message(FATAL_ERROR
    "Installed consumer executable was not produced: ${consumer_executable}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PATH=${install_prefix}/bin;$ENV{PATH}"
          "${consumer_executable}"
  RESULT_VARIABLE consumer_run_result
  OUTPUT_VARIABLE consumer_run_output
  ERROR_VARIABLE consumer_run_error)
file(WRITE "${test_root}/consumer-run.log"
  "${consumer_run_output}\n${consumer_run_error}")
if(NOT consumer_run_result EQUAL 0)
  message(FATAL_ERROR
    "Installed consumer exited with ${consumer_run_result}:\n"
    "${consumer_run_output}\n${consumer_run_error}")
endif()

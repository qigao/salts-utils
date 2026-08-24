cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS
    TP_MAIN_BINARY_DIR TP_CONSUMER_SOURCE_DIR TP_DEPENDENCY_VERIFIER
    TP_LINK_VERIFIER TP_TURBOUTILS_DIR TP_GENERATOR TP_CONFIG)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

set(negative_binary_dir
  "${TP_MAIN_BINARY_DIR}/tbe/tbe_cbind/install_consumer_dependency_negative")
set(negative_configure_args "")
if(TP_GENERATOR MATCHES "Ninja")
  list(APPEND negative_configure_args -DCMAKE_NINJA_FORCE_RESPONSE_FILE=ON)
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --fresh
    -S "${TP_CONSUMER_SOURCE_DIR}"
    -B "${negative_binary_dir}"
    -G "${TP_GENERATOR}"
    "-DTurboParser_DIR=${TP_MAIN_BINARY_DIR}"
    "-DTurboUtils_DIR=${TP_TURBOUTILS_DIR}"
    "-DCMAKE_BUILD_TYPE=${TP_CONFIG}"
    -DTBE_CBIND_TEST_INJECT_CONDITIONAL_DATABIND=ON
    ${negative_configure_args}
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "Dependency-negative fixture did not reach generated evidence:\n"
    "${configure_output}\n${configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DTP_EVIDENCE_DIR=${negative_binary_dir}/tbe-cbind-dependency-evidence
    -DTP_CONFIG=${TP_CONFIG}
    -DTP_EXPECT_FORBIDDEN=ON
    -P "${TP_DEPENDENCY_VERIFIER}"
  RESULT_VARIABLE verifier_result
  OUTPUT_VARIABLE verifier_output
  ERROR_VARIABLE verifier_error)
set(verifier_log "${verifier_output}\n${verifier_error}")
if(NOT verifier_result EQUAL 0 OR
   NOT verifier_log MATCHES "forbidden DataBind dependency")
  message(FATAL_ERROR
    "Conditional wrapper did not expose its indirect DataBind dependency:\n"
    "${verifier_log}")
endif()

set(negative_build_command
  "${CMAKE_COMMAND}" --build "${negative_binary_dir}"
  --config "${TP_CONFIG}" --verbose)
if(TP_GENERATOR MATCHES "Ninja")
  list(APPEND negative_build_command -- -d keeprsp)
endif()
execute_process(
  COMMAND ${negative_build_command}
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
set(link_log "${negative_binary_dir}/dependency-negative-build.log")
file(WRITE "${link_log}" "${build_output}\n${build_error}")
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR
    "Dependency-negative fixture failed to build:\n${build_output}\n${build_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DTP_BUILD_DIR=${negative_binary_dir}
    -DTP_LINK_LOG=${link_log}
    -DTP_LINK_TARGET=tbe_cbind_install_consumer
    -DTP_EXPECT_FORBIDDEN=ON
    -DTP_OUTPUT_FILE=${negative_binary_dir}/expanded-link-evidence.txt
    -P "${TP_LINK_VERIFIER}"
  RESULT_VARIABLE link_verifier_result
  OUTPUT_VARIABLE link_verifier_output
  ERROR_VARIABLE link_verifier_error)
set(link_verifier_log "${link_verifier_output}\n${link_verifier_error}")
if(NOT link_verifier_result EQUAL 0 OR
   NOT link_verifier_log MATCHES "forbidden DataBind dependency")
  message(FATAL_ERROR
    "Conditional wrapper did not reach expanded link arguments:\n"
    "${link_verifier_log}")
endif()

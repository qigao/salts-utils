cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS
    TP_MAIN_BINARY_DIR TP_CONSUMER_SOURCE_DIR TP_DEPENDENCY_VERIFIER
    TP_TURBOUTILS_DIR TP_GENERATOR TP_CONFIG)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

set(injection_cases
  "conditional-wrapper|TBE_CBIND_TEST_INJECT_CONDITIONAL_EXTRA_DEPENDENCY"
  "direct-extra|TBE_CBIND_TEST_INJECT_DIRECT_EXTRA_DEPENDENCY")
foreach(injection_case IN LISTS injection_cases)
  string(REPLACE "|" ";" case_fields "${injection_case}")
  list(GET case_fields 0 case_name)
  list(GET case_fields 1 injection_option)
  set(negative_binary_dir
    "${TP_MAIN_BINARY_DIR}/tbe/tbe_cbind/install_consumer_dependency_${case_name}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --fresh
      -S "${TP_CONSUMER_SOURCE_DIR}"
      -B "${negative_binary_dir}"
      -G "${TP_GENERATOR}"
      "-DTurboParser_DIR=${TP_MAIN_BINARY_DIR}"
      "-DTurboUtils_DIR=${TP_TURBOUTILS_DIR}"
      "-DCMAKE_BUILD_TYPE=${TP_CONFIG}"
      "-D${injection_option}=ON"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
  if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
      "Dependency contract case ${case_name} did not configure:\n"
      "${configure_output}\n${configure_error}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -DTP_EVIDENCE_DIR=${negative_binary_dir}/tbe-cbind-interface-evidence
      -DTP_CONFIG=${TP_CONFIG}
      -P "${TP_DEPENDENCY_VERIFIER}"
    RESULT_VARIABLE verifier_result
    OUTPUT_VARIABLE verifier_output
    ERROR_VARIABLE verifier_error)
  set(verifier_log "${verifier_output}\n${verifier_error}")
  if(verifier_result EQUAL 0 OR
     NOT verifier_log MATCHES "interface contract mismatch")
    message(FATAL_ERROR
      "Installed TbeCBind accepted ${case_name}:\n${verifier_log}")
  endif()
endforeach()

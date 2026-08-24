cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS
    TP_MAIN_BINARY_DIR TP_RUNNER_SCRIPT TP_GUARD_SCRIPT TP_GUARD_PROBE_SCRIPT
    TP_CONSUMER_SOURCE_DIR TP_TURBOUTILS_DIR TP_TURBOUTILS_RUNTIME_DIR
    TP_INSTALL_CMAKEDIR TP_INSTALL_LIBDIR
    TP_GENERATOR TP_CONFIG)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

file(REAL_PATH "${TP_MAIN_BINARY_DIR}" main_binary_dir)
if(NOT IS_DIRECTORY "${main_binary_dir}")
  message(FATAL_ERROR "Main build directory does not exist: ${main_binary_dir}")
endif()

set(safety_root "${main_binary_dir}/tbe/tbe_cbind/path_safety_sandbox")
cmake_path(NORMAL_PATH safety_root)
set(containment_main "${main_binary_dir}")
set(containment_root "${safety_root}")
if(WIN32)
  string(TOLOWER "${containment_main}" containment_main)
  string(TOLOWER "${containment_root}" containment_root)
endif()
cmake_path(IS_PREFIX containment_main "${containment_root}" NORMALIZE
  safety_root_contained)
if(NOT safety_root_contained)
  message(FATAL_ERROR "Path-safety sandbox escaped the main build tree")
endif()
if(IS_SYMLINK "${safety_root}")
  message(FATAL_ERROR "Path-safety sandbox must not be a symlink")
endif()

function(remove_test_link link_path sentinel_path)
  if(IS_SYMLINK "${link_path}")
    file(REMOVE "${link_path}")
  elseif(WIN32 AND EXISTS "${link_path}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env
        "TP_TEST_LINK=${link_path}"
        powershell.exe -NoProfile -NonInteractive -Command
        "Remove-Item -LiteralPath $env:TP_TEST_LINK -Force"
      RESULT_VARIABLE remove_result
      OUTPUT_VARIABLE remove_output
      ERROR_VARIABLE remove_error)
    if(NOT remove_result EQUAL 0)
      message(FATAL_ERROR
        "Could not remove the test junction itself:\n"
        "${remove_output}\n${remove_error}")
    endif()
  else()
    message(FATAL_ERROR "Expected a test link at: ${link_path}")
  endif()
  if(EXISTS "${link_path}" OR IS_SYMLINK "${link_path}")
    message(FATAL_ERROR "Test link still exists after link-only removal")
  endif()
  if(NOT "${sentinel_path}" STREQUAL "" AND
     NOT EXISTS "${sentinel_path}")
    message(FATAL_ERROR "Removing the test link deleted its target sentinel")
  endif()
endfunction()

if(EXISTS "${safety_root}")
  set(previous_link
    "${safety_root}/main/tbe/tbe_cbind/install_consumer")
  if(EXISTS "${previous_link}" OR IS_SYMLINK "${previous_link}")
    file(REAL_PATH "${previous_link}" previous_link_real)
    set(previous_link_compare "${previous_link}")
    set(previous_link_real_compare "${previous_link_real}")
    if(WIN32)
      string(TOLOWER "${previous_link_compare}" previous_link_compare)
      string(TOLOWER "${previous_link_real_compare}" previous_link_real_compare)
    endif()
    if(IS_SYMLINK "${previous_link}" OR
       NOT previous_link_compare STREQUAL previous_link_real_compare)
      remove_test_link("${previous_link}" "")
    endif()
  endif()
  file(REAL_PATH "${safety_root}" existing_safety_root)
  set(existing_compare "${existing_safety_root}")
  set(expected_compare "${safety_root}")
  if(WIN32)
    string(TOLOWER "${existing_compare}" existing_compare)
    string(TOLOWER "${expected_compare}" expected_compare)
  endif()
  if(NOT existing_compare STREQUAL expected_compare)
    message(FATAL_ERROR "Path-safety sandbox resolves through a link")
  endif()
  file(REMOVE_RECURSE "${safety_root}")
endif()

set(fake_main "${safety_root}/main")
set(dotdot_outside "${safety_root}/outside-dotdot")
file(MAKE_DIRECTORY "${fake_main}" "${dotdot_outside}")
file(WRITE "${dotdot_outside}/sentinel.txt" "preserve")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DTP_MAIN_BINARY_DIR=${fake_main}
    -DTP_TEST_ROOT=${fake_main}/../outside-dotdot
    -DTP_CONSUMER_SOURCE_DIR=${TP_CONSUMER_SOURCE_DIR}
    -DTP_TURBOUTILS_DIR=${TP_TURBOUTILS_DIR}
    -DTP_TURBOUTILS_RUNTIME_DIR=${TP_TURBOUTILS_RUNTIME_DIR}
    -DTP_INSTALL_CMAKEDIR=${TP_INSTALL_CMAKEDIR}
    -DTP_INSTALL_LIBDIR=${TP_INSTALL_LIBDIR}
    -DTP_GENERATOR=${TP_GENERATOR}
    -DTP_CONFIG=${TP_CONFIG}
    -P "${TP_RUNNER_SCRIPT}"
  RESULT_VARIABLE runner_result
  OUTPUT_VARIABLE runner_output
  ERROR_VARIABLE runner_error)
if(runner_result EQUAL 0)
  message(FATAL_ERROR "Runner accepted a caller-controlled dotdot sandbox")
endif()
set(runner_log "${runner_output}\n${runner_error}")
if(NOT runner_log MATCHES "TP_TEST_ROOT cannot override" OR
   NOT runner_log MATCHES "fixed directory")
  message(FATAL_ERROR
    "Runner failed before enforcing its fixed sandbox:\n${runner_log}")
endif()
if(NOT EXISTS "${dotdot_outside}/sentinel.txt")
  message(FATAL_ERROR
    "Runner followed a dotdot sandbox and deleted the outside sentinel")
endif()

function(expect_guard_rejection case_name candidate validate_only sentinel)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -DTP_GUARD_SCRIPT=${TP_GUARD_SCRIPT}
      -DTP_GUARD_MAIN_DIR=${fake_main}
      -DTP_GUARD_CANDIDATE=${candidate}
      -DTP_GUARD_VALIDATE_ONLY=${validate_only}
      -P "${TP_GUARD_PROBE_SCRIPT}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error)
  set(probe_log "${probe_output}\n${probe_error}")
  if(probe_result EQUAL 0 OR
     NOT probe_log MATCHES "Refusing unsafe install-consumer sandbox")
    message(FATAL_ERROR
      "${case_name} was not rejected by the path guard:\n${probe_log}")
  endif()
  if(NOT "${sentinel}" STREQUAL "" AND NOT EXISTS "${sentinel}")
    message(FATAL_ERROR "${case_name} deleted its outside sentinel")
  endif()
endfunction()

set(sibling "${safety_root}/MaInSibling")
file(MAKE_DIRECTORY "${sibling}")
file(WRITE "${sibling}/sentinel.txt" "preserve")
expect_guard_rejection("case-variant sibling" "${sibling}" FALSE
  "${sibling}/sentinel.txt")

expect_guard_rejection("main-directory equality" "${fake_main}" TRUE "")
cmake_path(GET fake_main ROOT_PATH filesystem_root)
expect_guard_rejection("filesystem root" "${filesystem_root}" TRUE "")

set(dotdot_probe "${fake_main}/../outside-probe")
file(MAKE_DIRECTORY "${safety_root}/outside-probe")
file(WRITE "${safety_root}/outside-probe/sentinel.txt" "preserve")
expect_guard_rejection("dotdot sibling" "${dotdot_probe}" FALSE
  "${safety_root}/outside-probe/sentinel.txt")

set(link_outside "${safety_root}/outside-link")
set(link_parent "${fake_main}/tbe/tbe_cbind")
set(link_candidate "${link_parent}/install_consumer")
file(MAKE_DIRECTORY "${link_parent}")
file(MAKE_DIRECTORY "${link_outside}")
file(WRITE "${link_outside}/sentinel.txt" "preserve")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E create_symlink
          "${link_outside}" "${link_candidate}"
  RESULT_VARIABLE link_result
  OUTPUT_QUIET ERROR_QUIET)
if(NOT link_result EQUAL 0 AND WIN32)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "TP_TEST_LINK=${link_candidate}"
      "TP_TEST_TARGET=${link_outside}"
      powershell.exe -NoProfile -NonInteractive -Command
      "$null = New-Item -ItemType Junction -Path $env:TP_TEST_LINK -Target $env:TP_TEST_TARGET"
    RESULT_VARIABLE link_result
    OUTPUT_QUIET ERROR_QUIET)
endif()
if(link_result EQUAL 0)
  expect_guard_rejection("symlink or junction escape" "${link_candidate}"
    FALSE "${link_outside}/sentinel.txt")
  remove_test_link("${link_candidate}" "${link_outside}/sentinel.txt")
else()
  message(STATUS
    "Skipping symlink/junction escape case: link creation unavailable")
endif()

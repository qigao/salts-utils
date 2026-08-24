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

function(path_is_explicit_reparse_point input_path output_variable)
  if(IS_SYMLINK "${input_path}")
    set(${output_variable} TRUE PARENT_SCOPE)
    return()
  endif()
  if(WIN32 AND EXISTS "${input_path}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "TP_TEST_PATH=${input_path}"
        powershell.exe -NoProfile -NonInteractive -Command
        "$item = Get-Item -LiteralPath $env:TP_TEST_PATH -Force; if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { 'REPARSE' } else { 'PLAIN' }"
      RESULT_VARIABLE attribute_result
      OUTPUT_VARIABLE attribute_output
      ERROR_VARIABLE attribute_error
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT attribute_result EQUAL 0 OR
       NOT attribute_output MATCHES "^(REPARSE|PLAIN)$")
      message(FATAL_ERROR
        "Could not prove path reparse attributes: ${input_path}\n"
        "${attribute_output}\n${attribute_error}")
    endif()
    if(attribute_output STREQUAL "REPARSE")
      set(${output_variable} TRUE PARENT_SCOPE)
      return()
    endif()
  endif()
  set(${output_variable} FALSE PARENT_SCOPE)
endfunction()

function(assert_trusted_existing_directory directory main_directory)
  if(NOT EXISTS "${directory}" AND NOT IS_SYMLINK "${directory}")
    return()
  endif()
  path_is_explicit_reparse_point("${directory}" is_reparse)
  if(is_reparse)
    message(FATAL_ERROR
      "Path-safety sandbox has an untrusted ancestor: ${directory}")
  endif()
  if(NOT IS_DIRECTORY "${directory}")
    message(FATAL_ERROR
      "Path-safety sandbox component is not a directory: ${directory}")
  endif()
  file(REAL_PATH "${directory}" directory_real)
  set(directory_compare "${directory}")
  set(directory_real_compare "${directory_real}")
  set(main_compare "${main_directory}")
  cmake_path(NORMAL_PATH directory_compare)
  cmake_path(NORMAL_PATH directory_real_compare)
  cmake_path(NORMAL_PATH main_compare)
  if(WIN32)
    string(TOLOWER "${directory_compare}" directory_compare)
    string(TOLOWER "${directory_real_compare}" directory_real_compare)
    string(TOLOWER "${main_compare}" main_compare)
  endif()
  if(NOT directory_compare STREQUAL directory_real_compare)
    message(FATAL_ERROR
      "Path-safety sandbox has an untrusted ancestor: ${directory}")
  endif()
  cmake_path(IS_PREFIX main_compare "${directory_real_compare}" NORMALIZE
    directory_contained)
  if(NOT directory_contained)
    message(FATAL_ERROR
      "Path-safety sandbox component escaped the main build: ${directory}")
  endif()
endfunction()

function(remove_test_link link_path sentinel_path)
  path_is_explicit_reparse_point("${link_path}" is_reparse)
  if(NOT is_reparse)
    message(FATAL_ERROR
      "Refusing link-only removal of a non-reparse leaf: ${link_path}")
  endif()
  if(IS_SYMLINK "${link_path}")
    file(REMOVE "${link_path}")
  elseif(WIN32)
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
    message(FATAL_ERROR "Unsupported reparse-point removal: ${link_path}")
  endif()
  if(EXISTS "${link_path}" OR IS_SYMLINK "${link_path}")
    message(FATAL_ERROR "Test link still exists after link-only removal")
  endif()
  if(NOT "${sentinel_path}" STREQUAL "" AND
     NOT EXISTS "${sentinel_path}")
    message(FATAL_ERROR "Removing the test link deleted its target sentinel")
  endif()
endfunction()

if(NOT IS_DIRECTORY "${TP_MAIN_BINARY_DIR}")
  message(FATAL_ERROR
    "Main build directory does not exist: ${TP_MAIN_BINARY_DIR}")
endif()
set(main_input "${TP_MAIN_BINARY_DIR}")
cmake_path(ABSOLUTE_PATH main_input NORMALIZE)
path_is_explicit_reparse_point("${main_input}" main_is_reparse)
if(main_is_reparse)
  message(FATAL_ERROR
    "Path-safety sandbox has an untrusted ancestor: ${main_input}")
endif()
file(REAL_PATH "${main_input}" main_binary_dir)
set(main_input_compare "${main_input}")
set(main_real_compare "${main_binary_dir}")
if(WIN32)
  string(TOLOWER "${main_input_compare}" main_input_compare)
  string(TOLOWER "${main_real_compare}" main_real_compare)
endif()
if(NOT main_input_compare STREQUAL main_real_compare)
  message(FATAL_ERROR
    "Path-safety sandbox has an untrusted ancestor: ${main_input}")
endif()
assert_trusted_existing_directory("${main_binary_dir}" "${main_binary_dir}")

set(safety_root "${main_binary_dir}/tbe/tbe_cbind/path_safety_sandbox")
cmake_path(NORMAL_PATH safety_root)
cmake_path(IS_PREFIX main_binary_dir "${safety_root}" NORMALIZE
  safety_root_contained)
if(NOT safety_root_contained)
  message(FATAL_ERROR "Path-safety sandbox escaped the main build tree")
endif()
foreach(trusted_component IN ITEMS
    "${main_binary_dir}/tbe"
    "${main_binary_dir}/tbe/tbe_cbind"
    "${safety_root}")
  assert_trusted_existing_directory(
    "${trusted_component}" "${main_binary_dir}")
endforeach()

if(EXISTS "${safety_root}")
  foreach(trusted_child_parent IN ITEMS
      "${safety_root}/main"
      "${safety_root}/main/tbe"
      "${safety_root}/main/tbe/tbe_cbind")
    assert_trusted_existing_directory(
      "${trusted_child_parent}" "${main_binary_dir}")
  endforeach()
  set(previous_link
    "${safety_root}/main/tbe/tbe_cbind/install_consumer")
  if(EXISTS "${previous_link}" OR IS_SYMLINK "${previous_link}")
    path_is_explicit_reparse_point("${previous_link}" previous_is_reparse)
    if(previous_is_reparse)
      remove_test_link("${previous_link}" "")
    endif()
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
set(link_kind "symbolic link")
if(NOT link_result EQUAL 0 AND WIN32)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "TP_TEST_LINK=${link_candidate}"
      "TP_TEST_TARGET=${link_outside}"
      powershell.exe -NoProfile -NonInteractive -Command
      "$null = New-Item -ItemType Junction -Path $env:TP_TEST_LINK -Target $env:TP_TEST_TARGET"
    RESULT_VARIABLE link_result
    OUTPUT_QUIET ERROR_QUIET)
  if(link_result EQUAL 0)
    set(link_kind "Windows junction")
  endif()
endif()
if(link_result EQUAL 0)
  message(STATUS "Path-safety escape case uses ${link_kind}")
  expect_guard_rejection("symlink or junction escape" "${link_candidate}"
    FALSE "${link_outside}/sentinel.txt")
  remove_test_link("${link_candidate}" "${link_outside}/sentinel.txt")
else()
  message(STATUS
    "Skipping symlink/junction escape case: link creation unavailable")
endif()

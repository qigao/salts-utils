cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS
    TP_MAIN_BINARY_DIR TP_PATH_SAFETY_SCRIPT TP_RUNNER_SCRIPT
    TP_GUARD_SCRIPT TP_GUARD_PROBE_SCRIPT TP_CONSUMER_SOURCE_DIR
    TP_TURBOUTILS_DIR TP_TURBOUTILS_RUNTIME_DIR TP_INSTALL_CMAKEDIR
    TP_INSTALL_LIBDIR TP_GENERATOR TP_CONFIG)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

function(assert_canonical_directory directory expected_parent)
  if(NOT IS_DIRECTORY "${directory}" OR IS_SYMLINK "${directory}")
    message(FATAL_ERROR "Unsafe ancestor fixture directory: ${directory}")
  endif()
  file(REAL_PATH "${directory}" directory_real)
  set(directory_normal "${directory}")
  set(parent_normal "${expected_parent}")
  cmake_path(NORMAL_PATH directory_normal)
  cmake_path(NORMAL_PATH parent_normal)
  if(WIN32)
    string(TOLOWER "${directory_real}" directory_real)
    string(TOLOWER "${directory_normal}" directory_normal)
    string(TOLOWER "${parent_normal}" parent_normal)
  endif()
  if(NOT directory_real STREQUAL directory_normal)
    message(FATAL_ERROR "Ancestor fixture resolves through a link: ${directory}")
  endif()
  cmake_path(IS_PREFIX parent_normal "${directory_real}" NORMALIZE contained)
  if(NOT contained)
    message(FATAL_ERROR "Ancestor fixture escaped the main build: ${directory}")
  endif()
endfunction()

function(assert_explicit_reparse_point link_path)
  if(IS_SYMLINK "${link_path}")
    return()
  endif()
  if(WIN32 AND EXISTS "${link_path}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "TP_TEST_LINK=${link_path}"
        powershell.exe -NoProfile -NonInteractive -Command
        "$item = Get-Item -LiteralPath $env:TP_TEST_LINK -Force; if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) { exit 1 }"
      RESULT_VARIABLE reparse_result
      OUTPUT_QUIET ERROR_QUIET)
    if(reparse_result EQUAL 0)
      return()
    endif()
  endif()
  message(FATAL_ERROR "Fixture leaf is not an explicit reparse point: ${link_path}")
endfunction()

function(remove_explicit_test_link link_path)
  assert_explicit_reparse_point("${link_path}")
  if(IS_SYMLINK "${link_path}")
    file(REMOVE "${link_path}")
  else()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "TP_TEST_LINK=${link_path}"
        powershell.exe -NoProfile -NonInteractive -Command
        "Remove-Item -LiteralPath $env:TP_TEST_LINK -Force"
      RESULT_VARIABLE remove_result
      OUTPUT_VARIABLE remove_output
      ERROR_VARIABLE remove_error)
    if(NOT remove_result EQUAL 0)
      message(FATAL_ERROR
        "Could not remove fixture junction:\n${remove_output}\n${remove_error}")
    endif()
  endif()
endfunction()

file(REAL_PATH "${TP_MAIN_BINARY_DIR}" main_binary_dir)
assert_canonical_directory("${main_binary_dir}" "${main_binary_dir}")
set(fixture_root
  "${main_binary_dir}/tbe/tbe_cbind/path_safety_ancestor_fixture")
cmake_path(NORMAL_PATH fixture_root)
cmake_path(IS_PREFIX main_binary_dir "${fixture_root}" NORMALIZE fixture_contained)
if(NOT fixture_contained)
  message(FATAL_ERROR "Ancestor fixture is outside the main build")
endif()

if(EXISTS "${fixture_root}")
  assert_canonical_directory("${fixture_root}" "${main_binary_dir}")
  if(EXISTS "${fixture_root}/main" OR
     IS_SYMLINK "${fixture_root}/main")
    assert_canonical_directory("${fixture_root}/main" "${main_binary_dir}")
  endif()
  set(stale_link "${fixture_root}/main/tbe")
  if(EXISTS "${stale_link}" OR IS_SYMLINK "${stale_link}")
    remove_explicit_test_link("${stale_link}")
  endif()
  file(REMOVE_RECURSE "${fixture_root}")
endif()

set(attack_main "${fixture_root}/main")
set(backing_root "${fixture_root}/backing")
set(ordinary_target
  "${backing_root}/tbe_cbind/path_safety_sandbox/main/tbe/tbe_cbind/install_consumer")
file(MAKE_DIRECTORY "${attack_main}" "${backing_root}/tbe_cbind/path_safety_sandbox/main/tbe/tbe_cbind")
file(WRITE "${ordinary_target}" "ordinary-file-must-remain")
set(ancestor_link "${attack_main}/tbe")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E create_symlink
    "${backing_root}" "${ancestor_link}"
  RESULT_VARIABLE link_result
  OUTPUT_QUIET ERROR_QUIET)
set(link_kind "symbolic link")
if(NOT link_result EQUAL 0 AND WIN32)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "TP_TEST_LINK=${ancestor_link}" "TP_TEST_TARGET=${backing_root}"
      powershell.exe -NoProfile -NonInteractive -Command
      "$null = New-Item -ItemType Junction -Path $env:TP_TEST_LINK -Target $env:TP_TEST_TARGET"
    RESULT_VARIABLE link_result
    OUTPUT_QUIET ERROR_QUIET)
  if(link_result EQUAL 0)
    set(link_kind "Windows junction")
  endif()
endif()
if(NOT link_result EQUAL 0)
  file(REMOVE_RECURSE "${fixture_root}")
  message(STATUS "Skipping ancestor-link case: link creation unavailable")
  return()
endif()
message(STATUS "Ancestor-link case uses ${link_kind}")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DTP_MAIN_BINARY_DIR=${attack_main}
    -DTP_RUNNER_SCRIPT=${TP_RUNNER_SCRIPT}
    -DTP_GUARD_SCRIPT=${TP_GUARD_SCRIPT}
    -DTP_GUARD_PROBE_SCRIPT=${TP_GUARD_PROBE_SCRIPT}
    -DTP_CONSUMER_SOURCE_DIR=${TP_CONSUMER_SOURCE_DIR}
    -DTP_TURBOUTILS_DIR=${TP_TURBOUTILS_DIR}
    -DTP_TURBOUTILS_RUNTIME_DIR=${TP_TURBOUTILS_RUNTIME_DIR}
    -DTP_INSTALL_CMAKEDIR=${TP_INSTALL_CMAKEDIR}
    -DTP_INSTALL_LIBDIR=${TP_INSTALL_LIBDIR}
    -DTP_GENERATOR=${TP_GENERATOR}
    -DTP_CONFIG=${TP_CONFIG}
    -P "${TP_PATH_SAFETY_SCRIPT}"
  RESULT_VARIABLE probe_result
  OUTPUT_VARIABLE probe_output
  ERROR_VARIABLE probe_error)
set(probe_log "${probe_output}\n${probe_error}")
set(ordinary_survived FALSE)
if(EXISTS "${ordinary_target}")
  set(ordinary_survived TRUE)
endif()

assert_canonical_directory("${fixture_root}" "${main_binary_dir}")
assert_canonical_directory("${attack_main}" "${main_binary_dir}")
remove_explicit_test_link("${ancestor_link}")
if(NOT EXISTS "${ordinary_target}")
  set(ordinary_survived FALSE)
endif()
file(REMOVE_RECURSE "${fixture_root}")

if(NOT ordinary_survived)
  message(FATAL_ERROR
    "Path-safety script touched an ordinary file through an ancestor link")
endif()
if(probe_result EQUAL 0 OR
   NOT probe_log MATCHES "untrusted ancestor")
  message(FATAL_ERROR
    "Path-safety script did not reject the ancestor before child access:\n${probe_log}")
endif()

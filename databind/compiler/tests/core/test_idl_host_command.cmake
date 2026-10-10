cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED DATABIND_SOURCE_ROOT OR
   NOT IS_DIRECTORY "${DATABIND_SOURCE_ROOT}")
  message(FATAL_ERROR "DATABIND_SOURCE_ROOT must be an existing source folder")
endif()

get_filename_component(_saltsutils_root
  "${DATABIND_SOURCE_ROOT}/.." ABSOLUTE)
include("${_saltsutils_root}/cmake/IDL.cmake")

# Deliberately use directories with spaces. This command must survive the
# native Windows environment's semicolon-separated PATH in the MSBuild case.
set(_runtime_root "${CMAKE_CURRENT_LIST_DIR}/host runtime with spaces")
set(_salts_root "${CMAKE_CURRENT_LIST_DIR}/salts runtime with spaces")
_saltsutils_idl_host_command(
  _host_command "${CMAKE_COMMAND}" "${_runtime_root}" "${_salts_root}")

if(WIN32)
  list(LENGTH _host_command _argc)
  if(NOT _argc EQUAL 11)
    message(FATAL_ERROR
      "Windows host command has ${_argc} args; expected 11, not split PATH")
  endif()
  foreach(_required IN ITEMS
      "--modify"
      "PATH=path_list_prepend:${_runtime_root}/bin"
      "PATH=path_list_prepend:${_runtime_root}/lib"
      "PATH=path_list_prepend:${_salts_root}/bin"
      "--")
    if(NOT _required IN_LIST _host_command)
      message(FATAL_ERROR "Missing Windows environment argument: ${_required}")
    endif()
  endforeach()
endif()

# The helper invokes the exact explicit host program, not a PATH lookup.
execute_process(
  COMMAND ${_host_command} -E echo "salts-idlc-host-command-ok"
  RESULT_VARIABLE _status
  OUTPUT_VARIABLE _output
  ERROR_VARIABLE _error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _status EQUAL 0 OR
   NOT _output STREQUAL "salts-idlc-host-command-ok")
  message(FATAL_ERROR
    "Host command failed: status=${_status}, output=${_output}, error=${_error}")
endif()
message(STATUS "IDL host command PATH construction passed")

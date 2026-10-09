cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED DATABIND_SOURCE_ROOT OR
   NOT IS_DIRECTORY "${DATABIND_SOURCE_ROOT}")
  message(FATAL_ERROR "DATABIND_SOURCE_ROOT must be a valid directory")
endif()

get_filename_component(_repository "${DATABIND_SOURCE_ROOT}" DIRECTORY)
set(_module "${_repository}/cmake/IDL.cmake")
if(NOT EXISTS "${_module}")
  message(FATAL_ERROR "missing public cmake/IDL.cmake")
endif()

# Configure real consumers so that format admission is tested as behavior,
# not by searching arbitrary source markers. All rejections precede host
# compiler discovery and may not create a generated output.
set(_root "${CMAKE_CURRENT_BINARY_DIR}/public-native-format-gates")
file(MAKE_DIRECTORY "${_root}")
foreach(_case IN ITEMS message_without_codec socket_without_codec
                       native_with_transport_without_codec
                       wasm_with_transport_without_codec
                       types_with_codec types_with_native)
  set(_dir "${_root}/${_case}")
  file(REMOVE_RECURSE "${_dir}")
  file(MAKE_DIRECTORY "${_dir}")
  file(WRITE "${_dir}/schema.schema"
       "schema Gate; message Packet { uint32 value; }")

  if(_case STREQUAL "message_without_codec")
    set(_selection "ARTIFACTS MESSAGE")
    set(_expected "requires explicit BINARY_CODEC")
  elseif(_case STREQUAL "native_with_transport_without_codec")
    set(_selection "ARTIFACTS NATIVE TRANSPORTS HTTP")
    set(_expected "Contract-only NATIVE Service cannot select transport")
  elseif(_case STREQUAL "socket_without_codec")
    set(_selection "TRANSPORTS SOCKET")
    set(_expected "requires explicit BINARY_CODEC")
  elseif(_case STREQUAL "wasm_with_transport_without_codec")
    set(_selection "ARTIFACTS WASM TRANSPORTS HTTP")
    set(_expected "Contract-only WASM Component cannot select formatted transports")
  elseif(_case STREQUAL "types_with_codec")
    set(_selection "ARTIFACTS TYPES BINARY_CODEC")
    set(_expected "Contract-only")
  else()
    set(_selection "ARTIFACTS TYPES NATIVE")
    set(_expected "Contract-only")
  endif()

  file(WRITE "${_dir}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.27)\n"
    "project(FormatGate LANGUAGES C)\n"
    "include(\"${_module}\")\n"
    "salts_idl_target(TARGET gate "
    "IDL \"${_dir}/schema.schema\" "
    "${_selection})\n")

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_dir}" -B "${_dir}/build"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr)
  if(_result EQUAL 0)
    message(FATAL_ERROR
      "${_case}: invalid selection unexpectedly configured successfully")
  endif()
  string(FIND "${_stderr}" "${_expected}" _expected_at)
  if(_expected_at EQUAL -1)
    message(FATAL_ERROR
      "${_case}: wrong fail-fast contract:\n${_stdout}\n${_stderr}")
  endif()
endforeach()
file(REMOVE_RECURSE "${_root}")
message(STATUS "Public Contract-only TYPES and explicit Binary CMake gates passed")

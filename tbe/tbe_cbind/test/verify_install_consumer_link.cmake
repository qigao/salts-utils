cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS TP_BUILD_DIR TP_LINK_LOG TP_LINK_TARGET)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()
if(NOT DEFINED TP_EXPECT_FORBIDDEN)
  set(TP_EXPECT_FORBIDDEN OFF)
endif()

set_property(GLOBAL PROPERTY TBE_CBIND_LINK_DEPENDENCY_NAMES "")
set_property(GLOBAL PROPERTY TBE_CBIND_LINK_DEPENDENCY_EVIDENCE "")

function(record_link_dependency dependency_name original_token)
  string(STRIP "${dependency_name}" dependency_name)
  string(REGEX REPLACE "^[\"']|[\"',]$" "" dependency_name
    "${dependency_name}")
  if("${dependency_name}" STREQUAL "")
    return()
  endif()
  set_property(GLOBAL APPEND PROPERTY TBE_CBIND_LINK_DEPENDENCY_NAMES
    "${dependency_name}")
  set_property(GLOBAL APPEND PROPERTY TBE_CBIND_LINK_DEPENDENCY_EVIDENCE
    "${dependency_name} <= ${original_token}")
endfunction()

function(record_library_path library_token)
  set(library_path "${library_token}")
  string(REGEX REPLACE "^[\"']|[\"',]$" "" library_path
    "${library_path}")
  file(TO_CMAKE_PATH "${library_path}" library_path)
  cmake_path(GET library_path FILENAME library_name)
  record_link_dependency("${library_name}" "${library_token}")
endfunction()

function(collect_link_dependencies command_text)
  separate_arguments(command_tokens NATIVE_COMMAND "${command_text}")
  set(expect_framework_name FALSE)
  set(skip_output_value FALSE)
  foreach(command_token IN LISTS command_tokens)
    if(command_token MATCHES " && ")
      collect_link_dependencies("${command_token}")
      continue()
    endif()
    if(skip_output_value)
      set(skip_output_value FALSE)
      continue()
    endif()
    string(TOLOWER "${command_token}" command_token_lower)
    if(command_token_lower STREQUAL "-o")
      set(skip_output_value TRUE)
      continue()
    endif()
    if(command_token_lower STREQUAL "-install_name" OR
       command_token_lower STREQUAL "-soname")
      set(skip_output_value TRUE)
      continue()
    endif()
    if(command_token_lower MATCHES
       "^(/out:|/implib:|/pdb:|/map:|--output=|-o.+)")
      continue()
    endif()
    if(expect_framework_name)
      record_link_dependency("${command_token}" "-framework ${command_token}")
      set(expect_framework_name FALSE)
      continue()
    endif()
    if(command_token_lower STREQUAL "-framework")
      set(expect_framework_name TRUE)
      continue()
    endif()
    if(command_token_lower MATCHES "^/defaultlib:(.+[.]lib)$")
      record_library_path("${CMAKE_MATCH_1}")
    elseif(command_token_lower MATCHES "^/wholearchive:(.+[.]lib)$")
      record_library_path("${CMAKE_MATCH_1}")
    elseif(command_token_lower MATCHES
           "^-wl,(-force_load|--whole-archive),(.+[.](a|dylib|so([.][0-9]+)*))$")
      record_library_path("${CMAKE_MATCH_2}")
    elseif(command_token_lower MATCHES "^-wl,")
      continue()
    elseif(command_token_lower MATCHES "^-l:?(.+)$")
      record_link_dependency("${CMAKE_MATCH_1}" "${command_token}")
    elseif(command_token_lower MATCHES
           "[.](lib|a|dylib|tbd|so([.][0-9]+)*)$")
      record_library_path("${command_token}")
    endif()
  endforeach()
endfunction()

file(STRINGS "${TP_LINK_LOG}" build_log_lines)
set(raw_link_evidence "")
foreach(build_log_line IN LISTS build_log_lines)
  string(TOLOWER "${build_log_line}" build_log_line_lower)
  string(TOLOWER "${TP_LINK_TARGET}" link_target_lower)
  if(build_log_line_lower MATCHES "${link_target_lower}" AND
     build_log_line_lower MATCHES
       "(/out:|link(\\.exe)?([ \"]|$)| -o[ =]|cmake_link_script)")
    string(APPEND raw_link_evidence "${build_log_line}\n")
    collect_link_dependencies("${build_log_line}")
  endif()
endforeach()
if("${raw_link_evidence}" STREQUAL "")
  message(FATAL_ERROR
    "Could not identify the ${TP_LINK_TARGET} link command in: ${TP_LINK_LOG}")
endif()

string(REGEX MATCHALL "@([^ \t\r\n\"]+|\"[^\"]+\")"
  response_references "${raw_link_evidence}")
foreach(response_reference IN LISTS response_references)
  string(REGEX REPLACE "^@\"?([^\"]+)\"?$" "\\1"
    response_path "${response_reference}")
  file(TO_CMAKE_PATH "${response_path}" response_path)
  if(NOT IS_ABSOLUTE "${response_path}")
    set(response_path "${TP_BUILD_DIR}/${response_path}")
  endif()
  cmake_path(NORMAL_PATH response_path)
  if(NOT EXISTS "${response_path}")
    message(FATAL_ERROR
      "Link command referenced an unavailable response file: ${response_path}")
  endif()
  file(READ "${response_path}" response_contents)
  string(APPEND raw_link_evidence
    "\nRESPONSE_FILE ${response_path}:\n${response_contents}\n")
  collect_link_dependencies("${response_contents}")
endforeach()

get_property(dependency_names GLOBAL PROPERTY TBE_CBIND_LINK_DEPENDENCY_NAMES)
get_property(dependency_evidence GLOBAL
  PROPERTY TBE_CBIND_LINK_DEPENDENCY_EVIDENCE)
if(DEFINED TP_OUTPUT_FILE AND NOT "${TP_OUTPUT_FILE}" STREQUAL "")
  file(WRITE "${TP_OUTPUT_FILE}" "${raw_link_evidence}")
endif()
if(DEFINED TP_DEPENDENCY_OUTPUT_FILE AND
   NOT "${TP_DEPENDENCY_OUTPUT_FILE}" STREQUAL "")
  string(REPLACE ";" "\n" dependency_evidence_text
    "${dependency_evidence}")
  file(WRITE "${TP_DEPENDENCY_OUTPUT_FILE}"
    "${dependency_evidence_text}\n")
endif()

set(forbidden_dependency "")
foreach(dependency_name IN LISTS dependency_names)
  string(TOLOWER "${dependency_name}" dependency_name_lower)
  if(dependency_name_lower MATCHES "databind|data_bind")
    set(forbidden_dependency "${dependency_name}")
    break()
  endif()
endforeach()
if(NOT "${forbidden_dependency}" STREQUAL "" AND TP_EXPECT_FORBIDDEN)
  message(STATUS
    "Detected forbidden DataBind dependency token: ${forbidden_dependency}")
  return()
endif()
if(NOT "${forbidden_dependency}" STREQUAL "")
  message(FATAL_ERROR
    "Installed consumer dependency tokens contain forbidden DataBind: "
    "${forbidden_dependency}")
endif()
if(TP_EXPECT_FORBIDDEN)
  message(FATAL_ERROR
    "Expected a forbidden DataBind dependency token, parsed: ${dependency_names}")
endif()
message(STATUS "Parsed link dependency tokens: ${dependency_names}")

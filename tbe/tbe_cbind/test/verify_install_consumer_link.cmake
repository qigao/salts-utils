cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS TP_BUILD_DIR TP_LINK_LOG TP_LINK_TARGET)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()
if(NOT DEFINED TP_EXPECT_FORBIDDEN)
  set(TP_EXPECT_FORBIDDEN OFF)
endif()

file(STRINGS "${TP_LINK_LOG}" build_log_lines)
set(expanded_link_evidence "")
foreach(build_log_line IN LISTS build_log_lines)
  string(TOLOWER "${build_log_line}" build_log_line_lower)
  string(TOLOWER "${TP_LINK_TARGET}" link_target_lower)
  if(build_log_line_lower MATCHES "${link_target_lower}" AND
     build_log_line_lower MATCHES
       "(/out:|link(\\.exe)?([ \"]|$)| -o[ =]|cmake_link_script)")
    string(APPEND expanded_link_evidence "${build_log_line}\n")
  endif()
endforeach()
if("${expanded_link_evidence}" STREQUAL "")
  message(FATAL_ERROR
    "Could not identify the ${TP_LINK_TARGET} link command in: ${TP_LINK_LOG}")
endif()

string(REGEX MATCHALL "@([^ \t\r\n\"]+|\"[^\"]+\")"
  response_references "${expanded_link_evidence}")
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
  string(APPEND expanded_link_evidence
    "\nRESPONSE_FILE ${response_path}:\n${response_contents}\n")
endforeach()
if(DEFINED TP_OUTPUT_FILE AND NOT "${TP_OUTPUT_FILE}" STREQUAL "")
  file(WRITE "${TP_OUTPUT_FILE}" "${expanded_link_evidence}")
endif()

string(TOLOWER "${expanded_link_evidence}" link_evidence_lower)
set(has_forbidden_dependency FALSE)
if(link_evidence_lower MATCHES "databind|data_bind")
  set(has_forbidden_dependency TRUE)
endif()
if(has_forbidden_dependency AND TP_EXPECT_FORBIDDEN)
  message(STATUS "Detected forbidden DataBind dependency in expanded link arguments")
  return()
endif()
if(has_forbidden_dependency)
  message(FATAL_ERROR
    "Installed consumer link arguments contain a forbidden DataBind dependency")
endif()
if(TP_EXPECT_FORBIDDEN)
  message(FATAL_ERROR
    "Expected a forbidden DataBind dependency in expanded link arguments")
endif()
message(STATUS "Expanded link arguments contain no DataBind dependency")

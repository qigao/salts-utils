cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS
    TP_GUARD_SCRIPT TP_GUARD_MAIN_DIR TP_GUARD_CANDIDATE)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

include("${TP_GUARD_SCRIPT}")
tbe_cbind_reset_install_consumer_sandbox(
  "${TP_GUARD_MAIN_DIR}" "${TP_GUARD_CANDIDATE}"
  "${TP_GUARD_VALIDATE_ONLY}")

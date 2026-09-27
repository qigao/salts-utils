if(NOT DEFINED OPENAPI_FILE OR NOT EXISTS "${OPENAPI_FILE}")
  message(FATAL_ERROR "installed OpenAPI artifact is missing: ${OPENAPI_FILE}")
endif()

file(READ "${OPENAPI_FILE}" _openapi)
foreach(_needle IN ITEMS
    "\"openapi\":\"3.1.0\""
    "\"/users/{id}\""
    "\"minimum\":1"
    "\"minLength\":2"
    "\"maxLength\":16"
    "\"404\"")
  string(FIND "${_openapi}" "${_needle}" _found)
  if(_found EQUAL -1)
    message(FATAL_ERROR
            "installed OpenAPI artifact missing expected contract: ${_needle}")
  endif()
endforeach()

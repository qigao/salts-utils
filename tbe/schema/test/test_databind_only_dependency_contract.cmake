string(CONCAT FORBIDDEN_TARGET "Salts::C" "Bind")
string(CONCAT FORBIDDEN_INCLUDE "<c" "bind/")
string(CONCAT FORBIDDEN_MACRO "C" "BIND_")
string(CONCAT FORBIDDEN_SYMBOL "c" "bind_")

file(GLOB_RECURSE POLICY_FILES LIST_DIRECTORIES FALSE
  "${PROJECT_SOURCE_DIR}/CMakeLists.txt"
  "${PROJECT_SOURCE_DIR}/*.cmake"
  "${PROJECT_SOURCE_DIR}/*.c"
  "${PROJECT_SOURCE_DIR}/*.h"
  "${PROJECT_SOURCE_DIR}/*.cpp"
  "${PROJECT_SOURCE_DIR}/*.hpp"
  "${PROJECT_SOURCE_DIR}/*.mustache")

foreach(FILE_PATH IN LISTS POLICY_FILES)
  file(RELATIVE_PATH RELATIVE_FILE_PATH "${PROJECT_SOURCE_DIR}" "${FILE_PATH}")
  if(FILE_PATH STREQUAL CMAKE_CURRENT_LIST_FILE OR
     RELATIVE_FILE_PATH MATCHES "^(salts|vcpkg)/" OR
     RELATIVE_FILE_PATH MATCHES "(^|/)(build[^/]*|install|bin|out|cmake-build-[^/]*|\\.vcpkg_installed|vcpkg_installed|conan-cache)/" OR
     RELATIVE_FILE_PATH MATCHES "^docs/superpowers/")
    continue()
  endif()
  file(READ "${FILE_PATH}" CONTENT)
  foreach(FORBIDDEN IN ITEMS "${FORBIDDEN_TARGET}" "${FORBIDDEN_INCLUDE}"
                             "${FORBIDDEN_MACRO}" "${FORBIDDEN_SYMBOL}")
    string(FIND "${CONTENT}" "${FORBIDDEN}" POSITION)
    if(NOT POSITION EQUAL -1)
      message(FATAL_ERROR "DataBind-only dependency violation: ${FILE_PATH}")
    endif()
  endforeach()
endforeach()

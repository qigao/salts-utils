cmake_minimum_required(VERSION 3.20)

project(tbe_cbind_install_consumer LANGUAGES C)

find_package(TurboParser CONFIG REQUIRED
  PATHS "${TurboParser_DIR}"
  NO_DEFAULT_PATH)

function(assert_no_databind_dependency target_name)
  get_property(visited GLOBAL PROPERTY TBE_CBIND_CONSUMER_VISITED)
  if(";${visited};" MATCHES ";${target_name};")
    return()
  endif()
  set_property(GLOBAL APPEND PROPERTY TBE_CBIND_CONSUMER_VISITED
    "${target_name}")
  set_property(GLOBAL APPEND PROPERTY TBE_CBIND_CONSUMER_CLOSURE
    "${target_name}")

  get_target_property(link_dependencies "${target_name}"
    INTERFACE_LINK_LIBRARIES)
  if(NOT link_dependencies OR
     link_dependencies STREQUAL "link_dependencies-NOTFOUND")
    return()
  endif()

  foreach(dependency IN LISTS link_dependencies)
    string(TOLOWER "${dependency}" dependency_lower)
    if(dependency_lower MATCHES "databind|data_bind")
      message(FATAL_ERROR
        "${target_name} has a forbidden DataBind dependency: ${dependency}")
    endif()
    string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1"
      dependency_target "${dependency}")
    string(REGEX REPLACE "^\\$<BUILD_INTERFACE:([^>]+)>$" "\\1"
      dependency_target "${dependency_target}")
    string(REGEX REPLACE "^\\$<INSTALL_INTERFACE:([^>]+)>$" "\\1"
      dependency_target "${dependency_target}")
    if(TARGET "${dependency_target}")
      assert_no_databind_dependency("${dependency_target}")
    endif()
  endforeach()
endfunction()

set_property(GLOBAL PROPERTY TBE_CBIND_CONSUMER_VISITED "")
set_property(GLOBAL PROPERTY TBE_CBIND_CONSUMER_CLOSURE "")
assert_no_databind_dependency(TurboParser::TbeCBind)
assert_no_databind_dependency(TurboParser::Parser)
get_property(tbe_cbind_consumer_closure GLOBAL PROPERTY
  TBE_CBIND_CONSUMER_CLOSURE)
file(WRITE "${CMAKE_BINARY_DIR}/tbe-cbind-link-closure.txt"
  "${tbe_cbind_consumer_closure}\n")
message(STATUS
  "TbeCBind consumer dependency closure: ${tbe_cbind_consumer_closure}")

add_executable(tbe_cbind_install_consumer main.c)
target_link_libraries(tbe_cbind_install_consumer PRIVATE
  TurboParser::TbeCBind
  TurboParser::Parser)
set_target_properties(tbe_cbind_install_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)

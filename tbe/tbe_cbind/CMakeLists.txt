set(TARGET_NAME tbe_cbind)

cmake_add_source(TBE_CBIND_SOURCES RECURSE
  DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include
  PATTERNS "*.c" "*.h")

add_library(${TARGET_NAME} STATIC ${TBE_CBIND_SOURCES})
target_include_directories(${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${TARGET_NAME}
  PRIVATE TurboParser::TbeSchema
  PUBLIC TurboUtils::CBind)
cmake_config_target(${TARGET_NAME}
  ALIAS TurboParser::TbeCBind
  FOLDER "tbe/tbe_cbind"
  EXPORT_NAME TbeCBind)

function(tbe_cbind_check_dependency_graph target_name)
  get_target_property(alias_target ${target_name} ALIASED_TARGET)
  if(alias_target)
    set(target_name ${alias_target})
  endif()
  get_property(visited GLOBAL PROPERTY TBE_CBIND_DEPENDENCY_VISITED)
  if(";${visited};" MATCHES ";${target_name};")
    return()
  endif()
  set_property(GLOBAL APPEND PROPERTY TBE_CBIND_DEPENDENCY_VISITED ${target_name})
  get_target_property(link_deps ${target_name} LINK_LIBRARIES)
  get_target_property(interface_deps ${target_name} INTERFACE_LINK_LIBRARIES)
  foreach(dep IN LISTS link_deps interface_deps)
    string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1" dep "${dep}")
    string(REGEX REPLACE "^\\$<BUILD_INTERFACE:([^>]+)>$" "\\1" dep "${dep}")
    if(dep MATCHES "DataBind|data_bind")
      message(FATAL_ERROR "${target_name} has forbidden DataBind dependency: ${dep}")
    endif()
    if(TARGET ${dep})
      tbe_cbind_check_dependency_graph(${dep})
    endif()
  endforeach()
endfunction()

set_property(GLOBAL PROPERTY TBE_CBIND_DEPENDENCY_VISITED "")
tbe_cbind_check_dependency_graph(${TARGET_NAME})

install(TARGETS ${TARGET_NAME}
  EXPORT TurboParserTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

cmake_add_test(test_tbe_cbind_public_api
  SOURCES test/test_tbe_cbind_public_api.c
  LIBS TurboParser::TbeCBind TurboUtils::TinyTest
  FOLDER "tbe/tbe_cbind/tests")
cmake_add_test(test_tbe_cbind_public_api_cpp
  SOURCES test/test_tbe_cbind_public_api_cpp.cpp
  LIBS TurboParser::TbeCBind TurboUtils::TinyTest
  FOLDER "tbe/tbe_cbind/tests")
set_target_properties(test_tbe_cbind_public_api_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)

cmake_add_test(test_tbe_cbind_schema
  SOURCES test/test_tbe_cbind_schema.c
  LIBS TurboParser::TbeCBind TurboUtils::TinyTest
  FOLDER "tbe/tbe_cbind/tests")

cmake_add_test(test_tbe_cbind_plan
  SOURCES test/test_tbe_cbind_plan.c test/tbe_cbind_multitu_fixture.c
  LIBS TurboParser::TbeCBind TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src
  FOLDER "tbe/tbe_cbind/tests")

cmake_add_test(test_tbe_cbind_decode
  SOURCES test/test_tbe_cbind_decode.c test/tbe_cbind_multitu_fixture.c
  LIBS TurboParser::TbeCBind TurboUtils::Core TurboUtils::TinyTest
  FOLDER "tbe/tbe_cbind/tests")

cmake_add_test(test_tbe_cbind_json
  SOURCES test/test_tbe_cbind_json.c
  LIBS TurboParser::TbeCBind TurboParser::Parser TurboUtils::TinyTest
  FOLDER "tbe/tbe_cbind/tests")

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME test_tbe_cbind_install_consumer_path_safety
    COMMAND ${CMAKE_COMMAND}
      -DTP_MAIN_BINARY_DIR=${CMAKE_BINARY_DIR}
      -DTP_RUNNER_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/run_install_consumer.cmake
      -DTP_GUARD_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/install_consumer_path_guard.cmake
      -DTP_GUARD_PROBE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/path_guard_probe.cmake
      -DTP_CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test/install_consumer
      -DTP_TURBOUTILS_DIR=${TurboUtils_DIR}
      -DTP_TURBOUTILS_RUNTIME_DIR=$<TARGET_FILE_DIR:TurboUtils::Core>
      -DTP_INSTALL_CMAKEDIR=${CMAKE_INSTALL_LIBDIR}/cmake/TurboParser
      -DTP_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
      -DTP_GENERATOR=${CMAKE_GENERATOR}
      -DTP_CONFIG=$<CONFIG>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/test_install_consumer_path_safety.cmake)
  set_tests_properties(test_tbe_cbind_install_consumer_path_safety PROPERTIES
    LABELS "tbe_cbind;install-consumer;security"
    RUN_SERIAL TRUE)

  add_test(NAME test_tbe_cbind_install_consumer_path_safety_ancestor
    COMMAND ${CMAKE_COMMAND}
      -DTP_MAIN_BINARY_DIR=${CMAKE_BINARY_DIR}
      -DTP_PATH_SAFETY_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/test_install_consumer_path_safety.cmake
      -DTP_RUNNER_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/run_install_consumer.cmake
      -DTP_GUARD_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/install_consumer_path_guard.cmake
      -DTP_GUARD_PROBE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/path_guard_probe.cmake
      -DTP_CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test/install_consumer
      -DTP_TURBOUTILS_DIR=${TurboUtils_DIR}
      -DTP_TURBOUTILS_RUNTIME_DIR=$<TARGET_FILE_DIR:TurboUtils::Core>
      -DTP_INSTALL_CMAKEDIR=${CMAKE_INSTALL_LIBDIR}/cmake/TurboParser
      -DTP_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
      -DTP_GENERATOR=${CMAKE_GENERATOR}
      -DTP_CONFIG=$<CONFIG>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/test_install_consumer_path_safety_ancestor.cmake)
  set_tests_properties(
    test_tbe_cbind_install_consumer_path_safety_ancestor PROPERTIES
    LABELS "tbe_cbind;install-consumer;security"
    RUN_SERIAL TRUE)

  add_test(NAME test_tbe_cbind_install_consumer_dependency_negative
    COMMAND ${CMAKE_COMMAND}
      -DTP_MAIN_BINARY_DIR=${CMAKE_BINARY_DIR}
      -DTP_CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test/install_consumer
      -DTP_DEPENDENCY_VERIFIER=${CMAKE_CURRENT_SOURCE_DIR}/test/verify_install_consumer_dependencies.cmake
      -DTP_LINK_VERIFIER=${CMAKE_CURRENT_SOURCE_DIR}/test/verify_install_consumer_link.cmake
      -DTP_TURBOUTILS_DIR=${TurboUtils_DIR}
      -DTP_GENERATOR=${CMAKE_GENERATOR}
      -DTP_CONFIG=$<CONFIG>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/test_install_consumer_dependency_negative.cmake)
  set_tests_properties(test_tbe_cbind_install_consumer_dependency_negative PROPERTIES
    LABELS "tbe_cbind;install-consumer;dependency-negative"
    RUN_SERIAL TRUE)

  add_test(NAME test_tbe_cbind_install_consumer
    COMMAND ${CMAKE_COMMAND}
      -DTP_MAIN_BINARY_DIR=${CMAKE_BINARY_DIR}
      -DTP_CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test/install_consumer
      -DTP_TURBOUTILS_DIR=${TurboUtils_DIR}
      -DTP_TURBOUTILS_RUNTIME_DIR=$<TARGET_FILE_DIR:TurboUtils::Core>
      -DTP_PREFIX_PATH=${CMAKE_PREFIX_PATH}
      -DTP_DATABIND_PATH_ONLY=ON
      -DTP_INSTALL_CMAKEDIR=${CMAKE_INSTALL_LIBDIR}/cmake/TurboParser
      -DTP_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
      -DTP_GENERATOR=${CMAKE_GENERATOR}
      -DTP_CONFIG=$<CONFIG>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/run_install_consumer.cmake)
  set_tests_properties(test_tbe_cbind_install_consumer PROPERTIES
    LABELS "tbe_cbind;install-consumer"
    RUN_SERIAL TRUE)
endif()

if(MSVC)
  target_link_options(test_tbe_cbind_plan PRIVATE /OPT:NOICF)
  target_link_options(test_tbe_cbind_decode PRIVATE /OPT:NOICF)
endif()

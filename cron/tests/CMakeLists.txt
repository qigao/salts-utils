set(TEST_NAME test_turbo_cron)

cmake_add_test(${TEST_NAME}
  SOURCES test_turbo_cron.c
  LIBS TurboParser::Cron TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "cron/tests")
set_target_properties(${TEST_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CRON_PACKAGE_SMOKE_DIR ${CMAKE_CURRENT_BINARY_DIR}/package_consumer_build)
set(CRON_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/package_consumer)
set(CRON_PACKAGE_BIN
    ${CRON_PACKAGE_SMOKE_DIR}/cron_package_consumer${CMAKE_EXECUTABLE_SUFFIX})
set(CRON_PACKAGE_CONFIGURE_ARGS
    -G "${CMAKE_GENERATOR}"
    -S ${CRON_PACKAGE_SOURCE_DIR}
    -B ${CRON_PACKAGE_SMOKE_DIR}
    -DTurboParser_DIR=${PROJECT_BINARY_DIR}
    -DTurboUtils_DIR=${TurboUtils_DIR}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_MT=${CMAKE_MT}
    -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

if(DEFINED VCPKG_INSTALLED_DIR AND NOT "${VCPKG_INSTALLED_DIR}" STREQUAL "")
  list(APPEND CRON_PACKAGE_CONFIGURE_ARGS
       -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR})
endif()

foreach(_sanitizer
        ENABLE_SANITIZER_ADDRESS
        ENABLE_SANITIZER_UNDEFINED
        ENABLE_SANITIZER_LEAK
        ENABLE_SANITIZER_THREAD
        ENABLE_SANITIZER_MEMORY)
  if(DEFINED ${_sanitizer})
    list(APPEND CRON_PACKAGE_CONFIGURE_ARGS
         -D${_sanitizer}=${${_sanitizer}})
  endif()
endforeach()

set(CRON_PACKAGE_TEST_ENV ${CMAKE_COMMAND} -E env)
if(WIN32)
  get_filename_component(CRON_PACKAGE_RC_DIR "${CMAKE_RC_COMPILER}" DIRECTORY)
  set(CRON_PACKAGE_TEST_PATH "${CRON_PACKAGE_RC_DIR};$ENV{PATH}")
  string(REPLACE ";" "\\;" CRON_PACKAGE_TEST_PATH
                 "${CRON_PACKAGE_TEST_PATH}")
  list(APPEND CRON_PACKAGE_TEST_ENV "PATH=${CRON_PACKAGE_TEST_PATH}")
endif()

add_test(
  NAME cron_package_smoke_configure
  COMMAND ${CRON_PACKAGE_TEST_ENV} ${CMAKE_COMMAND}
          ${CRON_PACKAGE_CONFIGURE_ARGS})

add_test(
  NAME cron_package_smoke_build
  COMMAND ${CRON_PACKAGE_TEST_ENV} ${CMAKE_COMMAND}
          --build ${CRON_PACKAGE_SMOKE_DIR}
          --target cron_package_consumer)

add_test(
  NAME cron_package_smoke_run
  COMMAND ${CRON_PACKAGE_BIN})

if(ENABLE_SANITIZER_ADDRESS AND NOT MSVC)
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=libasan.so
    OUTPUT_VARIABLE CRON_PACKAGE_ASAN_RUNTIME
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(CRON_PACKAGE_ASAN_RUNTIME AND NOT CRON_PACKAGE_ASAN_RUNTIME STREQUAL "libasan.so")
    set_property(TEST cron_package_smoke_run APPEND PROPERTY
                 ENVIRONMENT "LD_PRELOAD=${CRON_PACKAGE_ASAN_RUNTIME}")
  endif()
endif()

set_tests_properties(cron_package_smoke_configure PROPERTIES
  FIXTURES_SETUP cron_package_smoke
  LABELS "package;cron")

set_tests_properties(cron_package_smoke_build PROPERTIES
  FIXTURES_REQUIRED cron_package_smoke
  FIXTURES_SETUP cron_package_smoke_built
  LABELS "package;cron")

set_tests_properties(cron_package_smoke_run PROPERTIES
  FIXTURES_REQUIRED cron_package_smoke_built
  LABELS "package;cron"
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

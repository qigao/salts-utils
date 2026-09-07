foreach(required SOURCE_DIR BUILD_DIR INSTALL_CONFIG CMAKE_COMMAND_PATH
                 CTEST_COMMAND_PATH BUILD_GENERATOR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "VerifyInstalledPlayback requires ${required}")
  endif()
endforeach()

set(smoke_root "${BUILD_DIR}/playback-package-smoke")
set(install_prefix "${smoke_root}/install")
set(consumer_build "${smoke_root}/consumer")
file(REMOVE_RECURSE "${smoke_root}")

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --install "${BUILD_DIR}"
          --prefix "${install_prefix}" --config "${INSTALL_CONFIG}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SaltsUtils staging install failed: ${result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}"
          -S "${SOURCE_DIR}/playback/tests/package_consumer"
          -B "${consumer_build}"
          -G "${BUILD_GENERATOR}"
          "-DCMAKE_BUILD_TYPE=${INSTALL_CONFIG}"
          "-DSaltsUtils_DIR=${install_prefix}/lib/cmake/SaltsUtils"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Playback package consumer configure failed: ${result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --build "${consumer_build}"
          --config "${INSTALL_CONFIG}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Playback package consumer build failed: ${result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" -E env
          "PATH=${install_prefix}/bin;$ENV{PATH}"
          "${CTEST_COMMAND_PATH}" --test-dir "${consumer_build}"
          --build-config "${INSTALL_CONFIG}" --output-on-failure
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Playback package consumer test failed: ${result}")
endif()

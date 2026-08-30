foreach(required_var
        SOURCE_DIR
        BUILD_DIR
        CMAKE_COMMAND_PATH
        CMAKE_CTEST_COMMAND_PATH
        BUILD_CONFIG
        BUILD_GENERATOR
        TURBOUTILS_ROOT
        EXPECT_CAPTURE)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR
            "VerifyInstalledDeviceComponents requires ${required_var}")
  endif()
endforeach()

cmake_path(ABSOLUTE_PATH BUILD_DIR NORMALIZE OUTPUT_VARIABLE build_root)
set(smoke_root "${build_root}/device-package-smoke")
cmake_path(IS_PREFIX build_root "${smoke_root}" NORMALIZE smoke_is_in_build)
if(NOT smoke_is_in_build)
  message(FATAL_ERROR "device package smoke directory escaped the build tree")
endif()

set(install_prefix "${smoke_root}/install")
set(consumer_build "${smoke_root}/consumer")
file(REMOVE_RECURSE "${smoke_root}")

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --install "${build_root}"
          --prefix "${install_prefix}" --config "${BUILD_CONFIG}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "TurboParser package install failed: ${install_result}")
endif()

if(EXPECT_CAPTURE_WINDOWS_RUNTIME)
  foreach(capture_runtime_dependency libyuv.dll jpeg62.dll)
    set(capture_runtime_path
        "${install_prefix}/bin/${capture_runtime_dependency}")
    if(NOT EXISTS "${capture_runtime_path}")
      message(FATAL_ERROR
              "TurboParser Capture package is missing ${capture_runtime_path}")
    endif()
  endforeach()
endif()

file(GLOB installed_target_files
     "${install_prefix}/lib/cmake/TurboParser/TurboParserTargets*.cmake")
if(NOT installed_target_files)
  message(FATAL_ERROR "TurboParser installed target files were not generated")
endif()

set(forbidden_export_references "${SOURCE_DIR}" "${BUILD_DIR}"
                                "TurboUtils::Capture"
                                "TurboUtils::turbo_serial")
set(capture_export_found FALSE)
foreach(installed_target_file IN LISTS installed_target_files)
  file(READ "${installed_target_file}" installed_target_content)
  foreach(forbidden_reference IN LISTS forbidden_export_references)
    string(FIND "${installed_target_content}" "${forbidden_reference}"
           forbidden_reference_offset)
    if(NOT forbidden_reference_offset EQUAL -1)
      message(FATAL_ERROR
              "TurboParser export ${installed_target_file} contains private reference: ${forbidden_reference}")
    endif()
  endforeach()

  file(STRINGS "${installed_target_file}" installed_target_lines)
  set(in_capture_properties FALSE)
  foreach(installed_target_line IN LISTS installed_target_lines)
    if(installed_target_line MATCHES
       "^set_target_properties\\(TurboParser::Capture PROPERTIES$")
      set(in_capture_properties TRUE)
      set(capture_export_found TRUE)
    elseif(in_capture_properties AND installed_target_line MATCHES
                                             "^[ \t]*\\)$")
      set(in_capture_properties FALSE)
    elseif(in_capture_properties
           AND installed_target_line MATCHES
               "^  IMPORTED_LINK_DEPENDENT_LIBRARIES_[A-Z0-9_]+ \\\"(.*)\\\"$")
      set(capture_link_dependencies "${CMAKE_MATCH_1}")
      foreach(capture_link_dependency IN LISTS capture_link_dependencies)
        if(NOT capture_link_dependency STREQUAL "TurboUtils::Core")
          message(
            FATAL_ERROR
              "TurboParser::Capture export contains unapproved link dependency: ${capture_link_dependency}"
          )
        endif()
      endforeach()
    endif()
  endforeach()
endforeach()

if(EXPECT_CAPTURE AND NOT capture_export_found)
  message(FATAL_ERROR "TurboParser::Capture was not found in installed exports")
elseif(NOT EXPECT_CAPTURE AND capture_export_found)
  message(FATAL_ERROR "TurboParser::Capture leaked into a feature-off export")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}"
          -S "${SOURCE_DIR}/tests/install_consumer"
          -B "${consumer_build}"
          -G "${BUILD_GENERATOR}"
          "-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}"
          "-DTurboParser_DIR=${install_prefix}/lib/cmake/TurboParser"
          "-DTurboUtils_DIR=${TURBOUTILS_ROOT}/lib/cmake/TurboUtils"
          "-DTURBOPARSER_EXPECT_CAPTURE=${EXPECT_CAPTURE}"
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
          "TurboParser installed consumer configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --build "${consumer_build}"
          --config "${BUILD_CONFIG}"
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR
          "TurboParser installed consumer build failed: ${build_result}")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND_PATH}"
          --test-dir "${consumer_build}"
          --build-config "${BUILD_CONFIG}"
          --output-on-failure
  RESULT_VARIABLE consumer_test_result)
if(NOT consumer_test_result EQUAL 0)
  message(FATAL_ERROR
          "TurboParser installed consumer tests failed: ${consumer_test_result}")
endif()

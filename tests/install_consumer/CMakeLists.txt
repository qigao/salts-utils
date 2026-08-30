cmake_minimum_required(VERSION 3.27)

project(TurboParserDeviceInstallConsumer LANGUAGES C)

include(CTest)

function(add_runtime_test target_name)
  add_test(NAME ${target_name} COMMAND ${target_name})
  if(WIN32)
    set_property(
      TEST ${target_name}
      PROPERTY ENVIRONMENT_MODIFICATION
               "$<LIST:TRANSFORM,$<LIST:REVERSE,$<TARGET_RUNTIME_DLL_DIRS:${target_name}>>,PREPEND,PATH=path_list_prepend:>")
  endif()
endfunction()

find_package(TurboParser CONFIG REQUIRED)

if(NOT TARGET TurboParser::Serial)
  message(FATAL_ERROR "TurboParser::Serial is required")
endif()

add_executable(consume_serial consumer.c)
target_compile_features(consume_serial PRIVATE c_std_11)
target_compile_definitions(consume_serial PRIVATE CONSUME_SERIAL=1)
target_link_libraries(consume_serial PRIVATE TurboParser::Serial)
add_runtime_test(consume_serial)

if(TURBOPARSER_EXPECT_CAPTURE)
  if(NOT TARGET TurboParser::Capture)
    message(FATAL_ERROR
            "TurboParser::Capture is required when capture is enabled")
  endif()
  add_executable(consume_capture consumer.c)
  target_compile_features(consume_capture PRIVATE c_std_11)
  target_compile_definitions(consume_capture PRIVATE CONSUME_CAPTURE=1)
  target_link_libraries(consume_capture PRIVATE TurboParser::Capture)
  add_runtime_test(consume_capture)
elseif(TARGET TurboParser::Capture)
  message(FATAL_ERROR
          "TurboParser::Capture must not be exported when capture is disabled")
endif()

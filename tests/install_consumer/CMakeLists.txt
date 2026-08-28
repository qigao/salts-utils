cmake_minimum_required(VERSION 3.20)

project(TurboParserDeviceInstallConsumer LANGUAGES C)

find_package(TurboParser CONFIG REQUIRED)

if(NOT TARGET TurboParser::Serial)
  message(FATAL_ERROR "TurboParser::Serial is required")
endif()

add_executable(consume_serial consumer.c)
target_compile_features(consume_serial PRIVATE c_std_11)
target_compile_definitions(consume_serial PRIVATE CONSUME_SERIAL=1)
target_link_libraries(consume_serial PRIVATE TurboParser::Serial)

if(TURBOPARSER_EXPECT_CAPTURE)
  if(NOT TARGET TurboParser::Capture)
    message(FATAL_ERROR
            "TurboParser::Capture is required when capture is enabled")
  endif()
  add_executable(consume_capture consumer.c)
  target_compile_features(consume_capture PRIVATE c_std_11)
  target_compile_definitions(consume_capture PRIVATE CONSUME_CAPTURE=1)
  target_link_libraries(consume_capture PRIVATE TurboParser::Capture)
elseif(TARGET TurboParser::Capture)
  message(FATAL_ERROR
          "TurboParser::Capture must not be exported when capture is disabled")
endif()

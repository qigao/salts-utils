add_library(turbo_parser SHARED src/turbo_parser.c)
cmake_config_target(turbo_parser
  ALIAS TurboParser::Parser
  EXPORT_NAME Parser
  FOLDER turbo_parser)

target_include_directories(
  turbo_parser
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
          $<INSTALL_INTERFACE:include>
  )


target_link_libraries(
  turbo_parser
  PUBLIC TurboUtils::Core
   PRIVATE TurboUtils::CsvParser
          TurboUtils::IniParser
          TurboUtils::JsonParser
          TurboUtils::UriParser
          TurboUtils::TLVParser
          TurboUtils::LtvParser
          TurboUtils::ModbusParser
          TurboUtils::SoaParser
          TurboUtils::DotEnvParser
          TurboUtils::CmdParser
          TurboUtils::Toon
          TurboUtils::TomlParser
          TurboUtils::DateTimeParser
          TurboUtils::CYamlJsonAdapter
          TurboUtils::XmlParser)

target_compile_definitions(turbo_parser PRIVATE TURBO_PARSER_BUILD_DLL)

install(
  TARGETS turbo_parser
  EXPORT TurboParserTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp")

# Tests
cmake_add_test(test_turbo_parser
  SOURCES test/test_turbo_parser.c
  LIBS turbo_parser TurboUtils::TinyTest
  FOLDER turbo_parser)
cmake_add_test(test_turbo_parser_bdd
  SOURCES test/test_turbo_parser_bdd.c
  LIBS turbo_parser TurboUtils::TinyTest
  FOLDER turbo_parser)

if(BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

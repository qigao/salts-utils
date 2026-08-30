set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")

# =============================================================================
# Library
# =============================================================================
cmake_add_source(HEADER_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
                 PATTERNS "*.h")
cmake_add_source(SOURCE_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                 PATTERNS "*.c")
# Build as SHARED library (DLL)
add_library(mustache SHARED ${HEADER_FILES} ${SOURCE_FILES})
cmake_config_target(mustache
  ALIAS TurboParser::Mustache
  FOLDER "mustache"
  EXPORT_NAME Mustache)

target_compile_definitions(mustache PRIVATE MUSTACHE_BUILD_DLL)

target_include_directories(
  mustache
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include> $<INSTALL_INTERFACE:include/mustache>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link to json_parser and common
target_link_libraries(
  mustache
  PRIVATE TurboUtils::JsonParser TurboUtils::XmlParser
  PUBLIC TurboUtils::Core)

# Example executable

cmake_add_executable(mustache_json_example
  SOURCES examples/json_example.c
  LIBS TurboParser::Mustache TurboUtils::JsonParser
  FOLDER "mustache/examples")

cmake_add_executable(mustache_xml_example
  SOURCES examples/xml_example.c
  LIBS TurboParser::Mustache TurboUtils::XmlParser
  FOLDER "mustache/examples")

add_subdirectory(test)
if(BUILD_BENCHMARKS OR BUILD_TESTS)
  add_subdirectory(benchmarks)
endif()

install(
  TARGETS mustache
  EXPORT TurboParserTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/mustache.h include/mustache_json.h include/mustache_xml.h
              DESTINATION include/mustache)

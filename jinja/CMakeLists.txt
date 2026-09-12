set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")

cmake_add_grammar(JINJA_TEMPLATE
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/jinja_template_lexer.re
  LEXER_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/jinja_template_lexer.h
  FOLDER "jinja/grammar")
cmake_add_grammar(JINJA_EXPRESSION
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/jinja_expression_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/jinja_expression_grammar.y
  LEXER_DEPENDS_ON_GRAMMAR
  LEXER_OPTIONS -I ${RE2C_STDLIB_DIR}
  LEXER_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/jinja_expression_parser.h ${RE2C_UNICODE_PROPERTIES} ${RE2C_UNICODE_CATEGORIES}
  FOLDER "jinja/grammar")

set(JINJA_TEXT_LEXER_GEN "${CMAKE_CURRENT_BINARY_DIR}/jinja_text_lexer_gen.c")
add_custom_command(
  OUTPUT "${JINJA_TEXT_LEXER_GEN}"
  COMMAND "${RE2C_EXECUTABLE}" -I "${RE2C_STDLIB_DIR}" -o "${JINJA_TEXT_LEXER_GEN}"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/parser/jinja_text_lexer.re"
  DEPENDS src/parser/jinja_text_lexer.re src/parser/jinja_text_lexer.h ${RE2C_UNICODE_CATEGORIES}
  COMMENT "Generating Jinja text lexer with re2c"
  VERBATIM)
add_custom_target(jinja_text_lexer_codegen DEPENDS "${JINJA_TEXT_LEXER_GEN}")
set_target_properties(jinja_text_lexer_codegen PROPERTIES FOLDER "jinja/grammar")

add_library(
  jinja_cmeta SHARED
  include/jinja_cmeta.h
  include/jinja_cmeta_runtime.h
  src/jinja_cmeta_runtime.c
  src/jinja_cmeta_runtime_internal.h
  src/jinja_cmeta.c
  src/jinja_cmeta_environment.c
  src/jinja_cmeta_environment.h
  src/jinja_cmeta_layout.c
  src/jinja_cmeta_cells.c
  src/jinja_cmeta_cells.h
  src/jinja_cmeta_value.h
  src/jinja_cmeta_values.h
  src/jinja_cmeta_memory.h
  src/jinja_cmeta_provider.c
  src/jinja_cmeta_internal.h
  src/jinja_float.c
  src/jinja_float.h
  src/parser/jinja_expression_parser.c
  src/parser/jinja_expression_parser.h
  src/parser/jinja_expression_lexer.re
  src/parser/jinja_expression_grammar.y
  src/parser/jinja_template_lexer.h
  src/parser/jinja_template_parser.h
  src/parser/jinja_template_parser.c
  src/parser/jinja_template_lexer.re
  src/parser/jinja_text_lexer.re
  src/parser/jinja_text_lexer.h
  ${JINJA_TEXT_LEXER_GEN}
  ${JINJA_EXPRESSION_LEXER_GEN}
  ${JINJA_EXPRESSION_GRAMMAR_GEN}
  ${JINJA_TEMPLATE_LEXER_GEN})
add_dependencies(
  jinja_cmeta
  jinja_text_lexer_codegen
  ${JINJA_EXPRESSION_LEXER_TARGET}
  ${JINJA_EXPRESSION_GRAMMAR_TARGET}
  ${JINJA_TEMPLATE_LEXER_TARGET})

cmake_config_target(
  jinja_cmeta
  ALIAS Salts::JinjaCMeta
  FOLDER "jinja"
  EXPORT_NAME JinjaCMeta)

target_compile_definitions(jinja_cmeta PRIVATE JINJA_CMETA_BUILD_DLL)
target_include_directories(
  jinja_cmeta
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
         $<INSTALL_INTERFACE:include/jinja>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/src/parser
          ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(jinja_cmeta
  PUBLIC Salts::CMeta Salts::Core
  PRIVATE Salts::Unicode Salts::CSTL)
if(NOT WIN32)
  target_link_libraries(jinja_cmeta PRIVATE m)
endif()

install(
  TARGETS jinja_cmeta
  EXPORT SaltsUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/jinja_cmeta.h include/jinja_cmeta_runtime.h DESTINATION include/jinja)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

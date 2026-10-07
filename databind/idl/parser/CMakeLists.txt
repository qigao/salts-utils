cmake_add_grammar(SCHEMA
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE schema_lexer.re
  GRAMMAR_Y schema_grammar.y
  FOLDER "databind/idl/parser")

target_sources(${IDL_TARGET} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_lexer.re"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_grammar.y"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_lexer.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_types.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_size.h"
  "${SCHEMA_LEXER_GEN}"
  "${SCHEMA_GRAMMAR_GEN}")
add_dependencies(${IDL_TARGET} ${SCHEMA_LEXER_TARGET} ${SCHEMA_GRAMMAR_TARGET})
target_include_directories(${IDL_TARGET} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}")

set(SALTS_UNICODE_LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/src/salts_unicode.re)
set(SALTS_UNICODE_LEXER_GEN ${CMAKE_CURRENT_BINARY_DIR}/salts_unicode_gen.c)

add_custom_command(
  OUTPUT ${SALTS_UNICODE_LEXER_GEN}
  COMMAND ${RE2C_EXECUTABLE}
          --lang c
          --utf8
          --encoding-policy fail
          --no-generation-date
          -W
          -Werror
          -I "${RE2C_ROOT}/share/re2c/stdlib"
          -o ${SALTS_UNICODE_LEXER_GEN}
          ${SALTS_UNICODE_LEXER_RE}
  DEPENDS ${SALTS_UNICODE_LEXER_RE} ${RE2C_UNICODE_PROPERTIES} ${RE2C_UNICODE_CATEGORIES}
  COMMENT "Generating Unicode 17.0.0 UTF-8 scanner with re2c"
  VERBATIM)

add_library(salts_unicode SHARED
  include/salts_unicode.h
  src/salts_unicode_case.c
  src/unicode_case_data.h
  src/salts_unicode_names.c
  ${SALTS_UNICODE_LEXER_GEN})
cmake_config_target(
  salts_unicode
  ALIAS Salts::Unicode
  FOLDER "unicode"
  EXPORT_NAME Unicode)

target_compile_definitions(salts_unicode PRIVATE SALTS_UNICODE_BUILD_DLL)
target_include_directories(
  salts_unicode
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
         $<INSTALL_INTERFACE:include/unicode>)
target_link_libraries(salts_unicode PUBLIC Salts::Core)

install(
  TARGETS salts_unicode
  EXPORT SaltsUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/salts_unicode.h DESTINATION include/unicode)
install(FILES data/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/salts_unicode)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()

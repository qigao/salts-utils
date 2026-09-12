set(TEST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/../src)

set(JINJA_CMETA_CASE_SOURCES
  test_jinja_cmeta_predicates.c
  test_jinja_cmeta_lexical.c
  test_jinja_cmeta_escaping.c
  test_jinja_cmeta_loops.c
  test_jinja_cmeta_statements.c
  test_jinja_cmeta_scopes.c
  test_jinja_cmeta_ranges.c
  test_jinja_cmeta_iterators.c
  test_jinja_cmeta_attributes.c
  test_jinja_cmeta_callables.c
  test_jinja_cmeta_strings.c
  test_jinja_cmeta_reentrant.c
  test_jinja_cmeta_representations.c
  test_jinja_cmeta_budgets.c
  test_jinja_cmeta_blocks.c
  test_jinja_cmeta_identity.c
  test_jinja_cmeta_filters.c
  test_jinja_cmeta_collections.c
  test_jinja_cmeta_expressions.c
  test_jinja_cmeta_arithmetic.c
  test_jinja_cmeta_templates.c)

foreach(test_source IN LISTS JINJA_CMETA_CASE_SOURCES)
  get_filename_component(test_target "${test_source}" NAME_WE)
  cmake_add_test(${test_target}
    SOURCES ${test_source} jinja_cmeta_test_support.h
    LIBS Salts::JinjaCMeta Salts::TinyTest
    INCLUDES ${TEST_INCLUDES}
    FOLDER "jinja/tests")
  set_tests_properties(${test_target} PROPERTIES LABELS "jinja;cmeta;unit")
endforeach()

cmake_add_test(test_jinja_cmeta_header_cpp
  SOURCES test_jinja_cmeta_header_cpp.cpp
  LIBS Salts::JinjaCMeta Salts::TinyTest
  INCLUDES ${TEST_INCLUDES}
  FOLDER "jinja/tests")

set_tests_properties(test_jinja_cmeta_header_cpp PROPERTIES LABELS "jinja;cmeta;headers")

# Compile-only public runtime header check; it has no runtime test entry point.
add_library(test_jinja_runtime_header_cpp OBJECT test_jinja_runtime_header_cpp.cpp)
target_include_directories(test_jinja_runtime_header_cpp PRIVATE ${TEST_INCLUDES})
set_target_properties(test_jinja_runtime_header_cpp PROPERTIES FOLDER "jinja/tests")

# Compile private parser sources directly; no test-only symbols enter the public DLL API.
set(JINJA_EXPRESSION_TEST_SOURCES
  test_jinja_assignment_parser.c
  test_jinja_expression_memory.c
  test_jinja_header_memory.c)
foreach(test_source IN LISTS JINJA_EXPRESSION_TEST_SOURCES)
  get_filename_component(test_target "${test_source}" NAME_WE)
  cmake_add_test(${test_target}
    SOURCES ${test_source}
            ../src/parser/jinja_expression_parser.c
            ../src/jinja_float.c
            ${JINJA_EXPRESSION_LEXER_GEN}
            ${JINJA_EXPRESSION_GRAMMAR_GEN}
    LIBS Salts::Core Salts::Unicode Salts::CSTL Salts::TinyTest
    INCLUDES ${TEST_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/../src/parser
             ${CMAKE_CURRENT_BINARY_DIR}/..
    FOLDER "jinja/tests")
  set_tests_properties(${test_target} PROPERTIES LABELS "jinja;parser;unit")
endforeach()

set(JINJA_TEMPLATE_TEST_SOURCES
  test_jinja_template_parser.c
  test_jinja_template_memory.c)
foreach(test_source IN LISTS JINJA_TEMPLATE_TEST_SOURCES)
  get_filename_component(test_target "${test_source}" NAME_WE)
  cmake_add_test(${test_target}
    SOURCES ${test_source}
            ../src/parser/jinja_template_parser.c
            ../src/parser/jinja_expression_parser.c
            ../src/jinja_float.c
            ${JINJA_EXPRESSION_LEXER_GEN}
            ${JINJA_EXPRESSION_GRAMMAR_GEN}
            ${JINJA_TEMPLATE_LEXER_GEN}
    LIBS Salts::Core Salts::Unicode Salts::CSTL Salts::TinyTest
    INCLUDES ${TEST_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/../src/parser
             ${CMAKE_CURRENT_BINARY_DIR}/..
    FOLDER "jinja/tests")
  set_tests_properties(${test_target} PROPERTIES LABELS "jinja;parser;unit")
endforeach()

set(JINJA_NATIVE_TEST_SOURCES
  test_jinja_native_functions_size.c
  test_jinja_native_functions_execution.c
  test_jinja_native_functions_stable_cells.c
  test_jinja_native_functions_lowering.c)
foreach(test_source IN LISTS JINJA_NATIVE_TEST_SOURCES)
  get_filename_component(test_target "${test_source}" NAME_WE)
  cmake_add_test(${test_target}
    SOURCES ${test_source}
            ../src/jinja_cmeta.c ../src/jinja_cmeta_provider.c ../src/jinja_cmeta_layout.c ../src/jinja_float.c
            ../src/jinja_cmeta_cells.c ../src/jinja_cmeta_environment.c ../src/jinja_cmeta_runtime.c
            ../src/parser/jinja_template_parser.c ../src/parser/jinja_expression_parser.c
            ${JINJA_TEXT_LEXER_GEN}
            ${JINJA_EXPRESSION_LEXER_GEN} ${JINJA_EXPRESSION_GRAMMAR_GEN} ${JINJA_TEMPLATE_LEXER_GEN}
    LIBS Salts::CMeta Salts::Core Salts::CSTL Salts::Unicode Salts::TinyTest
    INCLUDES ${TEST_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/../src/parser ${CMAKE_CURRENT_BINARY_DIR}/..
    FOLDER "jinja/tests")
  add_dependencies(${test_target} jinja_text_lexer_codegen)
  if(NOT WIN32)
    target_link_libraries(${test_target} PRIVATE m)
  endif()
  set_tests_properties(${test_target} PROPERTIES LABELS "jinja;compiler;unit")
endforeach()

cmake_add_test(test_jinja_environment
  SOURCES test_jinja_environment.c
  LIBS Salts::JinjaCMeta Salts::TinyTest
  FOLDER "jinja/tests")
set_tests_properties(test_jinja_environment PROPERTIES LABELS "jinja;environment;unit")

cmake_add_test(test_jinja_value_storage
  SOURCES test_jinja_value_storage.c ../src/jinja_cmeta_cells.c
  LIBS Salts::JinjaCMeta Salts::CSTL Salts::TinyTest
  INCLUDES ${TEST_INCLUDES}
  FOLDER "jinja/tests")
set_tests_properties(test_jinja_value_storage PROPERTIES LABELS "jinja;storage;unit")

cmake_add_test(test_jinja_text_storage
  SOURCES test_jinja_text_storage.c
  LIBS Salts::JinjaCMeta Salts::CSTL Salts::TinyTest
  INCLUDES ${TEST_INCLUDES}
  FOLDER "jinja/tests")
set_tests_properties(test_jinja_text_storage PROPERTIES LABELS "jinja;storage;unit")

cmake_add_test(test_jinja_artifact_storage
  SOURCES test_jinja_artifact_storage.c
  LIBS Salts::JinjaCMeta Salts::TinyTest
  INCLUDES ${TEST_INCLUDES}
  FOLDER "jinja/tests")
set_tests_properties(test_jinja_artifact_storage PROPERTIES LABELS "jinja;compiler;storage;unit")

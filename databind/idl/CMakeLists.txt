set(IDL_TARGET databind_idl)

cmake_add_grammar(SCHEMA
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/schema_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/schema_grammar.y
  FOLDER "databind/idl/grammar")

# Full format-neutral IDL frontend and immutable typed Contract IR.
add_library(${IDL_TARGET} STATIC
  src/node_tree.c
  src/idl_contract.c
  src/idl_type_ref.c
  src/idl_contract_internal.h
  src/schema_enum.c
  src/schema_type_ref.c
  src/idl_frontend.c
  src/schema_service.c
  src/schema_channel.c
  src/schema_component.c
  src/tbe_error.c
  ${SCHEMA_LEXER_GEN}
  ${SCHEMA_GRAMMAR_GEN}
  ${SCHEMA_GRAMMAR_Y}
  ${SCHEMA_LEXER_RE})
add_dependencies(${IDL_TARGET} ${SCHEMA_LEXER_TARGET} ${SCHEMA_GRAMMAR_TARGET})
target_compile_features(${IDL_TARGET} PUBLIC c_std_11)
target_include_directories(
  ${IDL_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/parser
    ${DATABIND_SOURCE_ROOT}/schema/include
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${IDL_TARGET}
  PRIVATE Salts::Schema Salts::Core)

cmake_config_target(
  ${IDL_TARGET}
  ALIAS Salts::IDL
  FOLDER databind/idl
  EXPORT_NAME IDL)

install(
  TARGETS ${IDL_TARGET}
  EXPORT SaltsUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DataBind)

install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/idl_contract.h
  DESTINATION include
  COMPONENT DataBind)

if(BUILD_TESTS)
  cmake_add_test(test_idl_contract
    SOURCES test/test_idl_contract.c
    LIBS Salts::IDL Salts::TinyTest
    FOLDER "databind/idl/tests")
  cmake_add_test(test_component_contract
    SOURCES test/test_component_contract.c
    LIBS Salts::IDL Salts::TinyTest
    FOLDER "databind/idl/tests")
  cmake_add_test(test_channel_contract
    SOURCES test/test_channel_contract.c
    LIBS Salts::IDL Salts::TinyTest
    FOLDER "databind/idl/tests")
endif()

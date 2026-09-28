set(SCHEMA_TARGET databind_schema)

# Data-only Schema surface. Logical contracts are owned by Salts::IDL; this
# target only projects Data declarations into canonical CMeta semantics.
add_library(${SCHEMA_TARGET} STATIC
  src/schema_cmeta.c
  src/schema_cmeta_buffer.c)

target_compile_features(${SCHEMA_TARGET} PUBLIC c_std_11)
target_include_directories(
  ${SCHEMA_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${DATABIND_SOURCE_ROOT}/idl/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${SCHEMA_TARGET} PUBLIC Salts::Core)

cmake_config_target(
  ${SCHEMA_TARGET}
  ALIAS Salts::Schema
  FOLDER databind/schema
  EXPORT_NAME Schema)

install(
  TARGETS ${SCHEMA_TARGET}
  EXPORT SaltsUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DataBind)

install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/schema_cmeta.h
  DESTINATION include
  COMPONENT DataBind)

# =============================================================================
# Tests
# =============================================================================

set(DATABIND_SCHEMA_TEST_LIBS databind_tbe_contract Salts::TinyTest)
if(BUILD_TESTS)
foreach(test_name
        test_tbe_parser
        test_schema_boundaries
        test_schema_cmeta
        test_schema_cmeta_fields
        test_schema_enum_conformance
        test_flags_bdd
        test_tbe_parser_bdd
        test_benchmark_schemas_bdd
        test_tbe_robustness
        test_optional_and_defaults
        test_validation_constraints
        test_enum_helpers)
  cmake_add_test(${test_name}
    SOURCES test/${test_name}.c
    LIBS ${DATABIND_SCHEMA_TEST_LIBS}
    FOLDER "databind/schema/tests")
endforeach()


if(TARGET test_tbe_robustness)
  target_sources(test_tbe_robustness PRIVATE
    $<TARGET_OBJECTS:databind_tbe_format_objects>)
endif()

cmake_add_test(test_schema_cmeta_public_cpp
  SOURCES test/test_schema_cmeta_public_cpp.cpp
  LIBS Salts::Schema
  FOLDER "databind/schema/tests")
if(TARGET test_schema_cmeta_public_cpp)
  set_target_properties(test_schema_cmeta_public_cpp PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
endif()

if(TARGET test_schema_cmeta)
  target_sources(test_schema_cmeta PRIVATE
    test/test_schema_cmeta_buffer.c test/test_schema_cmeta_profiles.c)
  set_source_files_properties(test/test_schema_cmeta_buffer.c
    test/test_schema_cmeta_profiles.c
    PROPERTIES COMPILE_DEFINITIONS TINYTEST_NO_MAIN)
  add_test(NAME test_schema_cmeta_profile_contract
    COMMAND "${CMAKE_COMMAND}" -P
      "${CMAKE_CURRENT_SOURCE_DIR}/test/test_schema_cmeta_profile_contract.cmake")
endif()

cmake_add_executable(verify_fixes
  SOURCES test/verify_fixes.c
  LIBS databind_tbe_contract
  FOLDER databind/schema/tests)
target_sources(verify_fixes PRIVATE
  $<TARGET_OBJECTS:databind_tbe_format_objects>)

# =============================================================================
# 枚举增强功能测试：生成头文件
# =============================================================================

set(C_STRUCT_TEMPLATE ${DATABIND_SOURCE_ROOT}/idl/compiler/templates/c_structs.mustache)

set(ENUM_ENHANCED_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/test/test_enum_enhanced.schema)
set(ENUM_ENHANCED_HEADER ${CMAKE_BINARY_DIR}/bin/test_enum_enhanced.h)
add_custom_command(
  OUTPUT ${ENUM_ENHANCED_HEADER}
  COMMAND salts_idlc ${ENUM_ENHANCED_SCHEMA}
          --template ${C_STRUCT_TEMPLATE} --lang c --output ${ENUM_ENHANCED_HEADER}
  WORKING_DIRECTORY $<TARGET_FILE_DIR:salts_idlc>
  DEPENDS salts_idlc ${ENUM_ENHANCED_SCHEMA} ${C_STRUCT_TEMPLATE}
  COMMENT "Generating test_enum_enhanced.h"
  VERBATIM)

set(ENUM_ENHANCED_SIMPLE_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/test/test_enum_enhanced_simple.schema)
set(ENUM_ENHANCED_SIMPLE_HEADER ${CMAKE_BINARY_DIR}/bin/test_enum_enhanced_simple.h)
add_custom_command(
  OUTPUT ${ENUM_ENHANCED_SIMPLE_HEADER}
  COMMAND salts_idlc ${ENUM_ENHANCED_SIMPLE_SCHEMA}
          --template ${C_STRUCT_TEMPLATE} --lang c --output ${ENUM_ENHANCED_SIMPLE_HEADER}
  WORKING_DIRECTORY $<TARGET_FILE_DIR:salts_idlc>
  DEPENDS salts_idlc ${ENUM_ENHANCED_SIMPLE_SCHEMA} ${C_STRUCT_TEMPLATE}
  COMMENT "Generating test_enum_enhanced_simple.h"
  VERBATIM)

# 功能测试：枚举增强
cmake_add_test(test_enum_enhanced_functional
  SOURCES test/test_enum_enhanced_functional.c ${ENUM_ENHANCED_HEADER}
  LIBS ${DATABIND_SCHEMA_TEST_LIBS}
  INCLUDES ${CMAKE_BINARY_DIR}/bin
  FOLDER "databind/schema/tests")

# 功能测试：枚举增强（简化版）
cmake_add_test(test_enum_enhanced_simple_functional
  SOURCES test/test_enum_enhanced_simple_functional.c ${ENUM_ENHANCED_SIMPLE_HEADER}
  LIBS ${DATABIND_SCHEMA_TEST_LIBS}
  INCLUDES ${CMAKE_BINARY_DIR}/bin
  FOLDER "databind/schema/tests")

set(UNION_TYPES_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/test/test_union_types.schema)
set(UNION_TYPES_HEADER ${CMAKE_BINARY_DIR}/bin/test_union_types.h)
add_custom_command(
  OUTPUT ${UNION_TYPES_HEADER}
  COMMAND salts_idlc ${UNION_TYPES_SCHEMA}
          --template ${C_STRUCT_TEMPLATE} --lang c --output ${UNION_TYPES_HEADER}
  WORKING_DIRECTORY $<TARGET_FILE_DIR:salts_idlc>
  DEPENDS salts_idlc ${UNION_TYPES_SCHEMA} ${C_STRUCT_TEMPLATE}
  COMMENT "Generating test_union_types.h"
  VERBATIM)

# 功能测试：联合类型
cmake_add_test(test_union_types_functional
  SOURCES test/test_union_types_functional.c ${UNION_TYPES_HEADER}
  LIBS ${DATABIND_SCHEMA_TEST_LIBS}
  INCLUDES ${CMAKE_BINARY_DIR}/bin
  FOLDER "databind/schema/tests")

endif()

# =============================================================================
# Benchmarks
# =============================================================================

if(BUILD_BENCHMARKS)

cmake_add_benchmark(benchmark_tbe_simple
  SOURCES test/benchmark_tbe_simple.c
  LIBS ${DATABIND_SCHEMA_TEST_LIBS}
  FOLDER "databind/schema/benchmarks")

cmake_add_benchmark(benchmark_encoding
  SOURCES test/benchmark_encoding.c
  LIBS Salts::TinyTest
  FOLDER "databind/schema/benchmarks")

# =============================================================================
# TBE vs Native Struct Benchmark
# =============================================================================

set(BENCH_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/test/bench.schema)
set(BENCH_HEADER ${CMAKE_CURRENT_BINARY_DIR}/bench_data.h)
set(C_STRUCT_TEMPLATE
    ${DATABIND_SOURCE_ROOT}/idl/compiler/templates/c_structs.mustache)

add_custom_command(
  OUTPUT ${BENCH_HEADER}
  COMMAND salts_idlc ${BENCH_SCHEMA} --template ${C_STRUCT_TEMPLATE}
          --lang c --output ${BENCH_HEADER}
  DEPENDS salts_idlc ${BENCH_SCHEMA} ${C_STRUCT_TEMPLATE}
  COMMENT "Generating benchmark headers with salts_idlc"
  VERBATIM)

endif()

list(JOIN SANITIZER_LINK_FLAGS " " _databind_package_sanitizer_link_flags)
# Nested package tests must use the ABI and instrumentation of the exported tree.
set(_databind_package_test_args
  "-DSALTS_UTILS_PACKAGE_DIR=${CMAKE_BINARY_DIR}"
  "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
  "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
  "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
  "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
  "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
  "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
  "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
  "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS} ${_databind_package_sanitizer_link_flags}")

add_test(
  NAME databind_package_config_repeated_find
  COMMAND
    "${CMAKE_COMMAND}" -E env "SALTS_ROOT=${SALTS_ROOT_PATH}"
    "${CMAKE_COMMAND}" --fresh
    -S "${CMAKE_CURRENT_SOURCE_DIR}/databind_repeated_find"
    -B "${CMAKE_CURRENT_BINARY_DIR}/databind_repeated_find_build"
    -G "${CMAKE_GENERATOR}"
    ${_databind_package_test_args})

set_tests_properties(
  databind_package_config_repeated_find
  PROPERTIES LABELS "cmake;package-config;databind")

add_test(
  NAME databind_package_config_adapters
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "SALTS_ROOT=${SALTS_ROOT_PATH}"
    "${CMAKE_COMMAND}" --fresh
    -S "${CMAKE_CURRENT_SOURCE_DIR}/databind_adapters"
    -B "${CMAKE_CURRENT_BINARY_DIR}/databind_adapters_build"
    -G "${CMAKE_GENERATOR}"
    ${_databind_package_test_args})

set_tests_properties(
  databind_package_config_adapters
  PROPERTIES LABELS "cmake;package-config;databind")

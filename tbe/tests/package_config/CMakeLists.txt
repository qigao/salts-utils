add_test(
  NAME databind_package_config_repeated_find
  COMMAND
    "${CMAKE_COMMAND}" -E env "SALTS_ROOT=${SALTS_ROOT_PATH}"
    "${CMAKE_COMMAND}" --fresh
    -S "${CMAKE_CURRENT_SOURCE_DIR}/databind_repeated_find"
    -B "${CMAKE_CURRENT_BINARY_DIR}/databind_repeated_find_build"
    -G "${CMAKE_GENERATOR}"
    "-DSALTS_UTILS_PACKAGE_DIR=${CMAKE_BINARY_DIR}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")

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
    "-DSALTS_UTILS_PACKAGE_DIR=${CMAKE_BINARY_DIR}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")

set_tests_properties(
  databind_package_config_adapters
  PROPERTIES LABELS "cmake;package-config;databind")

add_test(
  NAME salts_utils_package_config_repeated_find
  COMMAND
    "${CMAKE_COMMAND}" -E env "SALTS_ROOT=${SALTS_ROOT_PATH}"
    "${CMAKE_COMMAND}" --fresh
    -S "${CMAKE_CURRENT_SOURCE_DIR}/repeated_find"
    -B "${CMAKE_CURRENT_BINARY_DIR}/repeated_find_build"
    -G "${CMAKE_GENERATOR}"
    "-DSALTS_UTILS_PACKAGE_DIR=${CMAKE_BINARY_DIR}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")

set_tests_properties(
  salts_utils_package_config_repeated_find
  PROPERTIES LABELS "cmake;package-config")

add_test(
  NAME salts_utils_package_config_resolves_public_dependencies
  COMMAND
    "${CMAKE_COMMAND}" -E env "SALTS_ROOT=${SALTS_ROOT_PATH}"
    "${CMAKE_COMMAND}" --fresh
    -S "${CMAKE_CURRENT_SOURCE_DIR}/public_dependencies"
    -B "${CMAKE_CURRENT_BINARY_DIR}/public_dependencies_build"
    -G "${CMAKE_GENERATOR}"
    "-DSALTS_UTILS_PACKAGE_DIR=${CMAKE_BINARY_DIR}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")

set_tests_properties(
  salts_utils_package_config_resolves_public_dependencies
  PROPERTIES LABELS "cmake;package-config")

add_test(
  NAME databind_package_core_only
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "SALTS_ROOT=${SALTS_ROOT_PATH}"
      "SALTS_UTILS_ROOT="
      "DATABIND_ROOT=${CMAKE_BINARY_DIR}"
    "${CMAKE_COMMAND}" --fresh
    -S "${CMAKE_CURRENT_SOURCE_DIR}/databind_core"
    -B "${CMAKE_CURRENT_BINARY_DIR}/databind_core_build"
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")

set_tests_properties(
  databind_package_core_only
  PROPERTIES LABELS "cmake;package-config;databind")

add_test(
  NAME databind_package_runtime_without_salts_utils
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "SALTS_ROOT=${SALTS_ROOT_PATH}"
      "SALTS_UTILS_ROOT="
      "DATABIND_ROOT=${CMAKE_BINARY_DIR}"
    "${CMAKE_COMMAND}" --fresh
    -S "${CMAKE_CURRENT_SOURCE_DIR}/databind_runtime"
    -B "${CMAKE_CURRENT_BINARY_DIR}/databind_runtime_build"
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
set_tests_properties(databind_package_runtime_without_salts_utils
  PROPERTIES LABELS "cmake;package-config;databind")

add_test(
  NAME databind_package_explicit_json_adapter
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "SALTS_ROOT=${SALTS_ROOT_PATH}"
      "SALTS_UTILS_ROOT=${CMAKE_BINARY_DIR}"
      "DATABIND_ROOT=${CMAKE_BINARY_DIR}"
    "${CMAKE_COMMAND}" --fresh
    -S "${CMAKE_CURRENT_SOURCE_DIR}/databind_adapter"
    -B "${CMAKE_CURRENT_BINARY_DIR}/databind_adapter_build"
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
set_tests_properties(databind_package_explicit_json_adapter
  PROPERTIES LABELS "cmake;package-config;databind")

add_test(
  NAME databind_package_installed_runtime_without_salts_utils
  COMMAND
    "${CMAKE_COMMAND}"
    "-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DSALTS_ROOT_PATH=${SALTS_ROOT_PATH}"
    "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
    "-DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_PREFIX_PATH_VALUE=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_TOOLCHAIN_FILE_VALUE=${CMAKE_TOOLCHAIN_FILE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_databind_installed_runtime.cmake")
set_tests_properties(
  databind_package_installed_runtime_without_salts_utils
  PROPERTIES LABELS "cmake;package-config;databind;install" RUN_SERIAL TRUE)

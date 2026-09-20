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
  NAME databind_install_component_partition
  COMMAND
    "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/install_component_partition"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/databind_install_component.cmake")

set_tests_properties(
  databind_install_component_partition
  PROPERTIES LABELS "cmake;package-config;databind;install")

add_test(
  NAME databind_parent_ownership_transition
  COMMAND
    "${CMAKE_COMMAND}"
    "-DPARENT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/databind_parent_ownership.cmake")

set_tests_properties(
  databind_parent_ownership_transition
  PROPERTIES LABELS "cmake;package-config;databind;transition")


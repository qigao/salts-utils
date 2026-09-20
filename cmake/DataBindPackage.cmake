# DataBind package ownership.
#
# This module lives inside the movable DataBind subtree so package generation,
# build-tree exports, and install-tree exports move with the runtime.

include(CMakePackageConfigHelpers)

set(DATABIND_PACKAGE_VERSION 3.0.0)

configure_package_config_file(
  "${DATABIND_SOURCE_ROOT}/cmake/DataBindConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/DataBindConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DataBind")

write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/DataBindConfigVersion.cmake"
  VERSION ${DATABIND_PACKAGE_VERSION}
  COMPATIBILITY SameMajorVersion)

export(
  EXPORT DataBindTargets
  FILE "${CMAKE_BINARY_DIR}/DataBindTargets.cmake"
  NAMESPACE Salts::)

export(
  EXPORT DataBindAdapterTargets
  FILE "${CMAKE_BINARY_DIR}/DataBindAdapterTargets.cmake"
  NAMESPACE Salts::)

install(
  EXPORT DataBindTargets
  FILE DataBindTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DataBind"
  COMPONENT DataBind)

install(
  EXPORT DataBindAdapterTargets
  FILE DataBindAdapterTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DataBind"
  COMPONENT DataBind)

install(
  FILES "${CMAKE_BINARY_DIR}/DataBindConfig.cmake"
        "${CMAKE_BINARY_DIR}/DataBindConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DataBind"
  COMPONENT DataBind)

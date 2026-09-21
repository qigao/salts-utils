# SaltsUtils-owned export sets for its DataBind/TBE component.
# These files are implementation details of SaltsUtilsConfig.cmake. They are
# installed together with SaltsUtils; no DataBind package/config is generated.

export(
  EXPORT DataBindTargets
  FILE "${CMAKE_BINARY_DIR}/SaltsUtilsDataBindTargets.cmake"
  NAMESPACE Salts::)

export(
  EXPORT DataBindAdapterTargets
  FILE "${CMAKE_BINARY_DIR}/SaltsUtilsDataBindAdapterTargets.cmake"
  NAMESPACE Salts::)

install(
  EXPORT DataBindTargets
  FILE SaltsUtilsDataBindTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils")

install(
  EXPORT DataBindAdapterTargets
  FILE SaltsUtilsDataBindAdapterTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils")

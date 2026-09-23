set(DATA_BIND_TBE_FORMAT_TARGET data_bind_tbe_format)

add_library(
  ${DATA_BIND_TBE_FORMAT_TARGET} STATIC
  src/tbe_version.c
  include/tbe_version.h
  include/tbe_endian.h
  include/tbe_wire.h)

target_compile_features(${DATA_BIND_TBE_FORMAT_TARGET} PUBLIC c_std_11)

target_include_directories(
  ${DATA_BIND_TBE_FORMAT_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

cmake_config_target(
  ${DATA_BIND_TBE_FORMAT_TARGET}
  ALIAS Salts::DataBindTbeFormat
  FOLDER databind/formats/tbe
  EXPORT_NAME DataBindTbeFormat)

install(
  TARGETS ${DATA_BIND_TBE_FORMAT_TARGET}
  EXPORT SaltsUtilsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DataBind)

install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION include
  COMPONENT DataBind
  FILES_MATCHING
  PATTERN "*.h")

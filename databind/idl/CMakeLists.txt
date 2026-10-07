set(IDL_TARGET databind_idl)

# Full format-neutral IDL frontend and immutable typed Contract IR.
add_library(${IDL_TARGET} STATIC)

add_subdirectory(core)
add_subdirectory(contract)
add_subdirectory(frontend)
add_subdirectory(parser)

target_compile_features(${IDL_TARGET} PUBLIC c_std_11)
target_include_directories(
  ${IDL_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${DATABIND_SOURCE_ROOT}/schema/include)
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

set(TEMPLATE_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/c_guest_adapter.mustache"
  "${CMAKE_CURRENT_SOURCE_DIR}/c_structs.mustache"
  "${CMAKE_CURRENT_SOURCE_DIR}/c_typed_source.mustache")

target_sources(salts_idlc PRIVATE ${TEMPLATE_FILES})

install(FILES ${TEMPLATE_FILES}
  DESTINATION "${CMAKE_INSTALL_BINDIR}/templates/c"
  COMPONENT DataBind)

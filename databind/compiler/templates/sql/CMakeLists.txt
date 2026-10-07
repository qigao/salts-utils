set(TEMPLATE_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/sqlite_schema.mustache"
  "${CMAKE_CURRENT_SOURCE_DIR}/postgresql_schema.mustache")

target_sources(salts_idlc PRIVATE ${TEMPLATE_FILES})

install(FILES ${TEMPLATE_FILES}
  DESTINATION "${CMAKE_INSTALL_BINDIR}/templates/sql"
  COMPONENT DataBind)

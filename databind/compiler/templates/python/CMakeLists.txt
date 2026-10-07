set(TEMPLATE_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/python_dataclass.mustache")

target_sources(salts_idlc PRIVATE ${TEMPLATE_FILES})

install(FILES ${TEMPLATE_FILES}
  DESTINATION "${CMAKE_INSTALL_BINDIR}/templates/python"
  COMPONENT DataBind)

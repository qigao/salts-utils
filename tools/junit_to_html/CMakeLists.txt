cmake_add_executable(junit_to_html
  SOURCES junit_to_html.c
  LIBS TurboUtils::Core TurboParser::Mustache TurboParser::JsonParser
       TurboParser::CmdParser cxml
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_SOURCE_DIR}/mustache/include
           ${CMAKE_SOURCE_DIR}/parser/json_parser/include
           ${CMAKE_SOURCE_DIR}/parser/cmd_parser/include
           ${CMAKE_SOURCE_DIR}/vendor/cxml/include
  FOLDER "tools")

add_custom_command(
  TARGET junit_to_html POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/dashboard.html
          $<TARGET_FILE_DIR:junit_to_html>/dashboard.html
  COMMENT "Copying JUnit dashboard template"
  VERBATIM)

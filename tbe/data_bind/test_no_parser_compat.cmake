if(NOT DEFINED DATA_BIND_SOURCE_DIR)
  message(FATAL_ERROR "DATA_BIND_SOURCE_DIR is required")
endif()

if(EXISTS "${DATA_BIND_SOURCE_DIR}/parser_compat")
  message(FATAL_ERROR "DataBind must use salts parsers directly; parser_compat still exists")
endif()

foreach(source_file IN ITEMS CMakeLists.txt data_bind.c data_bind.h tbe_typed.c)
  file(READ "${DATA_BIND_SOURCE_DIR}/${source_file}" source_text)
  if(source_text MATCHES "parser_compat|data_bind_parser_compat|turbo_parser\\.h")
    message(FATAL_ERROR "${source_file} still references DataBind parser compatibility code")
  endif()
endforeach()

target_sources(${IDL_TARGET} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/idl_frontend.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_enum.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_enum.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_type_ref.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_type_ref.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_service.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_service.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_channel.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_channel.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_component.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/schema_component.h")

target_include_directories(${IDL_TARGET}
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

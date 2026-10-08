target_sources(${IDL_TARGET} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/idl_contract.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/idl_type_ref.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/idl_type_check.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/idl_contract_internal.h")

target_include_directories(${IDL_TARGET}
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

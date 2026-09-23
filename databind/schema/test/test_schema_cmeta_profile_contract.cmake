# #45: scalar classification belongs to the canonical descriptor. This is a
# source-structure guard; runtime and parser behavior are tested in TinyTest.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../parser/schema_builtin_type.h" source)
string(REGEX MATCH
  "typedef[ \t\r\n]+struct[ \t\r\n]+schema_builtin_type_info[ \t\r\n]*\\{[^}]*\\}"
  profile "${source}")
if(profile STREQUAL "")
  message(FATAL_ERROR "Cannot locate the scalar wire-profile declaration")
endif()
foreach(field IN ITEMS is_integer is_unsigned is_float)
  if(profile MATCHES "(^|[^A-Za-z0-9_])${field}([^A-Za-z0-9_]|$)")
    message(FATAL_ERROR
      "Scalar wire profiles must derive ${field} from the canonical CMeta descriptor")
  endif()
endforeach()
if(NOT profile MATCHES "const[ \t]+cmeta_data_desc[ \t]*\\*[ \t]*data[ \t]*;")
  message(FATAL_ERROR "Scalar wire profiles must retain their canonical descriptor")
endif()
message(STATUS "Scalar classification has no duplicate wire-profile fields")

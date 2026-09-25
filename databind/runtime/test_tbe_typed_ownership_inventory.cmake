if(NOT DEFINED TBE_TYPED_HEADER OR NOT DEFINED OWNERSHIP_DOC)
  message(FATAL_ERROR "TBE_TYPED_HEADER and OWNERSHIP_DOC are required")
endif()

file(READ "${TBE_TYPED_HEADER}" header)
file(READ "${OWNERSHIP_DOC}" ownership)

string(REGEX MATCHALL "TbeTyped[A-Za-z0-9_]*" typed_symbols "${header}")
string(REGEX MATCHALL "TBE_TYPED_[A-Z0-9_]+" macro_symbols "${header}")
string(REGEX MATCHALL "tbe_typed_[a-z0-9_]+" function_symbols "${header}")
string(REGEX MATCHALL "tbe_bytes_t" bytes_symbols "${header}")

set(public_symbols
  ${typed_symbols}
  ${macro_symbols}
  ${function_symbols}
  ${bytes_symbols})
list(REMOVE_DUPLICATES public_symbols)
list(SORT public_symbols)

set(missing)
foreach(symbol IN LISTS public_symbols)
  string(FIND "${ownership}" "`${symbol}`" position)
  if(position EQUAL -1)
    list(APPEND missing "${symbol}")
  endif()
endforeach()

if(missing)
  list(JOIN missing ", " missing_text)
  message(FATAL_ERROR
    "Historical typed public identifiers are missing from "
    "docs/DATABIND_TYPED_OWNERSHIP.md: ${missing_text}")
endif()

list(LENGTH public_symbols symbol_count)
message(STATUS
  "DataBind historical typed ownership inventory covers ${symbol_count} identifiers")

# Run the real CLI twice per gated schema. A rejection may not replace either
# pre-existing output, and must retain the same schema/type diagnostic.
if(NOT DEFINED TBE_COMPILER OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "TBE_COMPILER and WORK_DIR are required")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}")
set(types datetime date time duration decimal bigint money MadeUp union)
foreach(type IN LISTS types)
  if(type STREQUAL "union")
    set(schema "composite Point { int32 x; } union Choice { Point point; } message Gate { Choice value; }")
    set(context "union declarations")
  else()
    set(schema "message Gate { ${type} value; }")
    set(context "Gate[.]value")
  endif()
  file(WRITE "${WORK_DIR}/${type}.schema" "${schema}")
  file(WRITE "${WORK_DIR}/${type}.h" "existing header\n")
  file(WRITE "${WORK_DIR}/${type}.c" "existing source\n")
  foreach(attempt RANGE 1 2)
    execute_process(COMMAND "${TBE_COMPILER}" "${WORK_DIR}/${type}.schema"
      --lang c --output "${WORK_DIR}/${type}.h" --source-output "${WORK_DIR}/${type}.c"
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic)
    if(result EQUAL 0 OR NOT diagnostic MATCHES "${context}" OR NOT diagnostic MATCHES "${type}")
      message(FATAL_ERROR "${type}: wrong rejection (${result}): ${diagnostic}")
    endif()
    file(READ "${WORK_DIR}/${type}.h" header)
    file(READ "${WORK_DIR}/${type}.c" source)
    if(NOT header STREQUAL "existing header\n" OR NOT source STREQUAL "existing source\n")
      message(FATAL_ERROR "${type}: rejected generation changed caller output")
    endif()
    if(attempt EQUAL 2 AND NOT diagnostic STREQUAL first_diagnostic)
      message(FATAL_ERROR "${type}: rejection diagnostic changed between attempts")
    endif()
    set(first_diagnostic "${diagnostic}")
  endforeach()
endforeach()

# Native typed C has one container storage authority: canonical Salts CSTL.
# Shapes that still lack a canonical provider must fail before either caller
# output is replaced; do not fall back to TBE raw-vector/private providers.
set(container_gate_names uuid_list uuid_set uuid_map record_set)
foreach(case IN LISTS container_gate_names)
  if(case STREQUAL "uuid_list")
    set(schema "message Gate { list<uuid> value; }")
  elseif(case STREQUAL "uuid_set")
    set(schema "message Gate { set<uuid> value; }")
  elseif(case STREQUAL "uuid_map")
    set(schema "message Gate { map<string,uuid> value; }")
  elseif(case STREQUAL "record_set")
    set(schema "message Item { string name; } message Gate { set<Item> value; }")
  endif()

  file(WRITE "${WORK_DIR}/${case}.schema" "${schema}")
  file(WRITE "${WORK_DIR}/${case}.h" "existing header\n")
  file(WRITE "${WORK_DIR}/${case}.c" "existing source\n")
  foreach(attempt RANGE 1 2)
    execute_process(COMMAND "${TBE_COMPILER}" "${WORK_DIR}/${case}.schema"
      --lang c --output "${WORK_DIR}/${case}.h"
      --source-output "${WORK_DIR}/${case}.c"
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic)
    if(result EQUAL 0 OR
       NOT diagnostic MATCHES "Gate[.]value" OR
       NOT diagnostic MATCHES "canonical CSTL")
      message(FATAL_ERROR
        "${case}: wrong canonical-container rejection (${result}): ${diagnostic}")
    endif()
    file(READ "${WORK_DIR}/${case}.h" header)
    file(READ "${WORK_DIR}/${case}.c" source)
    if(NOT header STREQUAL "existing header\n" OR
       NOT source STREQUAL "existing source\n")
      message(FATAL_ERROR
        "${case}: rejected container generation changed caller output")
    endif()
    if(attempt EQUAL 2 AND NOT diagnostic STREQUAL first_container_diagnostic)
      message(FATAL_ERROR
        "${case}: container rejection diagnostic changed between attempts")
    endif()
    set(first_container_diagnostic "${diagnostic}")
  endforeach()
endforeach()

# Canonical profiles, including DataBind optional/nullable overlays, must still
# generate successfully. Generated providers are compiled and exercised by the
# generated CMeta public and native collection tests.
set(canonical_schema
  "message Canonical { list<uint32> values; set<string> tags; map<string,uint32> attrs; optional list<uint32> maybe_values; nullable set<string> maybe_tags; optional nullable map<string,uint32> maybe_attrs; }")
file(WRITE "${WORK_DIR}/canonical_containers.schema" "${canonical_schema}")
execute_process(COMMAND "${TBE_COMPILER}" "${WORK_DIR}/canonical_containers.schema"
  --lang c
  --output "${WORK_DIR}/canonical_containers.h"
  --source-output "${WORK_DIR}/canonical_containers.c"
  RESULT_VARIABLE canonical_result
  OUTPUT_VARIABLE canonical_output
  ERROR_VARIABLE canonical_diagnostic)
if(NOT canonical_result EQUAL 0)
  message(FATAL_ERROR
    "canonical containers failed generation: ${canonical_diagnostic}")
endif()
message(STATUS
  "CMeta CLI gates: 9 semantic schemas + 4 unsupported container profiles rejected; canonical CSTL containers generated")

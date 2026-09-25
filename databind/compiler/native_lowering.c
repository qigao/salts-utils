#include "native_lowering.h"

#include <salts_cmeta_data.h>

#include <string.h>

int databind_native_field_lower_generated_c(
    const databind_semantic_field_ir *semantic,
    databind_native_field_ir *out) {
  databind_native_field_ir result = {0};

  if (semantic == NULL || out == NULL ||
      semantic->struct_size < sizeof(*semantic) ||
      semantic->abi_version != DATABIND_SEMANTIC_IR_ABI_VERSION)
    return 0;

  /*
   * Presence/nullability are logical Contract semantics. The native overlay
   * lowering for those states is intentionally not admitted in this slice.
   */
  if (semantic->is_optional || semantic->is_nullable) return 0;

  /*
   * DataBind generated C has an explicit representation policy for logical
   * string: unique-owned tstr. Salts 1.6 already publishes the matching
   * canonical CMeta storage/lifecycle provider, so selecting it here is an
   * explicit compile-time lowering decision rather than runtime inference.
   */
  if (semantic->semantic.kind == CMETA_DATA_STRING &&
      semantic->semantic.schema_kind != NULL &&
      strcmp(semantic->semantic.schema_kind, "string") == 0) {
    result.struct_size = sizeof(result);
    result.abi_version = DATABIND_NATIVE_FIELD_IR_ABI_VERSION;
    result.c_storage_type = "tstr";
    result.native_data = &salts_tstr_cmeta_data;
    result.native_type = &salts_tstr_cmeta_type;
    result.native_data_symbol = "salts_tstr_cmeta_data";
    result.native_type_symbol = "salts_tstr_cmeta_type";
    result.provider_external = 1;
    *out = result;
    return 1;
  }

  return 0;
}

#ifndef DATABIND_COMPILER_NATIVE_LOWERING_H
#define DATABIND_COMPILER_NATIVE_LOWERING_H

#include "semantic_ir.h"

#include <cmeta/data.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATABIND_NATIVE_FIELD_IR_ABI_VERSION 1u

/*
 * Generated-C native lowering result.
 *
 * This is projection IR, not Contract Semantic IR. It may contain C spelling
 * and emitter symbol names because its sole purpose is to tell the generated-C
 * and CMeta renderers which concrete native representation/provider was
 * selected for one canonical logical field.
 *
 * No Binary/JSON/XML/transport metadata belongs here.
 */
typedef struct databind_native_field_ir {
  size_t struct_size;
  unsigned abi_version;
  const char *c_storage_type;
  const cmeta_data_desc *native_data;
  const cmeta_type_desc *native_type;
  const char *native_data_symbol;
  const char *native_type_symbol;
  int provider_external;
} databind_native_field_ir;

/*
 * Lower one canonical field to the generated-C native representation.
 *
 * Returns non-zero only when this slice has an explicit, admitted provider.
 * Unsupported/gated storage returns zero and leaves *out unchanged. There is
 * no provider inference or fallback.
 */
int databind_native_field_lower_generated_c(
    const databind_semantic_field_ir *semantic,
    databind_native_field_ir *out);

#ifdef __cplusplus
}
#endif

#endif

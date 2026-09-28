#ifndef DATABIND_COMPILER_MESSAGE_NATIVE_H
#define DATABIND_COMPILER_MESSAGE_NATIVE_H

#include "idl_contract.h"

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct databind_compiler_message_native_state {
  char *field_name;
  unsigned bit;
} databind_compiler_message_native_state;

typedef struct databind_compiler_message_native_binding {
  char *schema_name;
  char *type_name;
  char *type_identity;
  databind_compiler_message_native_state *presence;
  size_t presence_count;
  databind_compiler_message_native_state *nulls;
  size_t null_count;
} databind_compiler_message_native_binding;

/*
 * Build one transport-/Service-neutral generated native binding model from the
 * canonical typed IDL contract. Presence/null state is compiled from logical
 * field order and does not depend on a format-specific rendering tree.
 */
int databind_compiler_message_native_build(
    const IdlContract *contract,
    const char *type_name,
    databind_compiler_message_native_binding *out);

void databind_compiler_message_native_destroy(
    databind_compiler_message_native_binding *binding);

/*
 * Emit one static DataBindNativeTypeBinding initializer function.
 *
 * symbol_prefix must be a collision-free C identifier supplied by the caller.
 * The generated function is:
 *   <symbol_prefix>__databind_message_native_binding(...)
 *
 * It borrows the existing generated <Type>_cmeta_data() graph and emits only
 * DataBind-owned presence/null overlay metadata.
 */
int databind_compiler_message_native_emit_binding(
    FILE *file,
    const databind_compiler_message_native_binding *binding,
    const char *symbol_prefix);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_MESSAGE_NATIVE_H */

#ifndef DATABIND_COMPILER_NATIVE_SOURCE_IR_H
#define DATABIND_COMPILER_NATIVE_SOURCE_IR_H

#include "idl_contract.h"
#include <stddef.h>

/* Compiler-private, deliberately narrow Contract-only Native lowering.
 * No Binary offsets, wire byte order or ownership assumptions.
 * Unsupported fields fail closed; caller owns no copied contract storage. */
/* Native ownership is independent of Binary layout. Keep ZERO/TRIVIAL as
 * the default so existing scalar fixture initializers remain valid.
 * No owning kind may enter the scalar-only renderer. */
typedef enum databind_native_source_ownership {
  DATABIND_NATIVE_TRIVIAL = 0,
  DATABIND_NATIVE_OWNED_TEXT,
  DATABIND_NATIVE_OWNED_BYTES,
  DATABIND_NATIVE_OWNED_RECORD,
  DATABIND_NATIVE_OWNED_SEQUENCE,
  DATABIND_NATIVE_OWNED_MAP,
  DATABIND_NATIVE_OWNED_SET
} databind_native_source_ownership;

typedef struct databind_native_source_field {
  const char *name;
  const char *c_type;
  int optional;
  int nullable;
  databind_native_source_ownership ownership;
  /* Borrowed canonical logical element name for sequence fields. This is
   * metadata only: an owning CSTL element trait is required for rendering. */
  const char *element_type;
  int element_is_trivial;
  /* Canonical CMeta descriptor symbol for a scalar CSTL element. NULL
   * means provider admission is unresolved; never synthesize a descriptor. */
  const char *element_cmeta_symbol;
  const char *key_type;
  const char *value_type;
  const char *key_cmeta_symbol;
  const char *value_cmeta_symbol;
} databind_native_source_field;

typedef struct databind_native_source_record {
  const char *name;
  size_t field_count;
  databind_native_source_field *fields;
} databind_native_source_record;

typedef struct databind_native_source_ir {
  size_t record_count;
  databind_native_source_record *records;
  /* Borrowed, immutable Contract namespace. A named Schema is mandatory
   * for stable cross-translation-unit CMeta type identity. */
  const char *schema_name;
  const char *schema_version; /* Optional Contract version, borrowed. */
} databind_native_source_ir;

int databind_native_source_ir_build(
    const IdlContract *contract, databind_native_source_ir *out);
void databind_native_source_ir_destroy(databind_native_source_ir *ir);
/* Render a self-contained scalar C header. Presence/null-state flags are
 * native representation only, not Binary wire layout. */
int databind_native_source_ir_write_header(
    const databind_native_source_ir *ir, const char *path);

#endif

#ifndef DATABIND_COMPILER_SEMANTIC_IR_H
#define DATABIND_COMPILER_SEMANTIC_IR_H

#include "node_tree.h"
#include "schema_cmeta.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATABIND_SEMANTIC_IR_ABI_VERSION 1u

typedef struct databind_semantic_field_ir {
  size_t struct_size;
  unsigned abi_version;
  const char *owner_name;
  const char *name;
  const char *declared_type;
  schema_cmeta_field_type semantic;

  /* Canonical logical state. These are Contract semantics, not native storage
   * or wire representation choices. */
  int is_optional;
  int is_nullable;
} databind_semantic_field_ir;

/*
 * Normalize one parsed schema field into the compiler's canonical typed
 * semantic view. Returned strings and CMeta descriptors are borrowed from the
 * parsed schema / provider-owned reflection storage and remain valid only while
 * those owners remain alive.
 *
 * This layer intentionally contains no C/C++/TS spelling, TBE wire kind,
 * template key, Lua/QuickJS metadata, or generated declaration text.
 */
int databind_semantic_field_build(const Node *root, const Node *field,
                                  databind_semantic_field_ir *out);

#ifdef __cplusplus
}
#endif

#endif

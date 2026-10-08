#ifndef DATABIND_COMPILER_NATIVE_SOURCE_IR_H
#define DATABIND_COMPILER_NATIVE_SOURCE_IR_H

#include "idl_contract.h"
#include <stddef.h>

/* Compiler-private, deliberately narrow Contract-only Native lowering.
 * No Binary offsets, wire byte order or ownership assumptions.
 * Unsupported fields fail closed; caller owns no copied contract storage. */
typedef struct databind_native_source_field {
  const char *name;
  const char *c_type;
} databind_native_source_field;

typedef struct databind_native_source_record {
  const char *name;
  size_t field_count;
  databind_native_source_field *fields;
} databind_native_source_record;

typedef struct databind_native_source_ir {
  size_t record_count;
  databind_native_source_record *records;
} databind_native_source_ir;

int databind_native_source_ir_build(
    const IdlContract *contract, databind_native_source_ir *out);
void databind_native_source_ir_destroy(databind_native_source_ir *ir);

#endif

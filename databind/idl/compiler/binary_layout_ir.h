#ifndef DATABIND_COMPILER_BINARY_LAYOUT_IR_H
#define DATABIND_COMPILER_BINARY_LAYOUT_IR_H

#include "binary_format_plan.h"
#include "idl_contract.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum databind_binary_layout_status {
  DATABIND_BINARY_LAYOUT_OK = 0,
  DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT,
  DATABIND_BINARY_LAYOUT_TYPE_NOT_FOUND,
  DATABIND_BINARY_LAYOUT_INVALID_SCHEMA,
  DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY
} databind_binary_layout_status;

typedef enum databind_binary_field_layout_kind {
  DATABIND_BINARY_FIELD_FIXED = 0,
  DATABIND_BINARY_FIELD_GROUP = 1,
  DATABIND_BINARY_FIELD_VAR_DATA = 2
} databind_binary_field_layout_kind;

/*
 * Binary-private scalar token representation derived from canonical Contract /
 * CMeta semantics. This is not a second public DataBind type system.
 *
 * NONE means the field is not a scalar (for example an inline fixed record).
 * Fixed record lowering resolves the canonical Contract child
 * and validates its independent BinaryLayoutIR; unsupported structural forms
 * remain fail-closed without guessing scalar semantics from their extent.
 */
typedef enum databind_binary_scalar_kind {
  DATABIND_BINARY_SCALAR_NONE = 0,
  DATABIND_BINARY_SCALAR_BOOL,
  DATABIND_BINARY_SCALAR_SINT,
  DATABIND_BINARY_SCALAR_UINT,
  DATABIND_BINARY_SCALAR_FLOAT,
  DATABIND_BINARY_SCALAR_ENUM_SINT,
  DATABIND_BINARY_SCALAR_ENUM_UINT,
  DATABIND_BINARY_SCALAR_STRING,
  DATABIND_BINARY_SCALAR_BYTES
} databind_binary_scalar_kind;

enum {
  DATABIND_BINARY_FIELD_OPTIONAL = 1u << 0,
  DATABIND_BINARY_FIELD_NULLABLE = 1u << 1
};

typedef struct databind_binary_field_layout {
  char *field_id;
  databind_binary_field_layout_kind kind;

  /* FIXED only: exact wire range inside the fixed block. */
  size_t wire_offset;
  size_t wire_extent;

  /*
   * Canonical semantic representation. FIXED scalar bits come from CMeta
   * integer/float metadata or enum underlying storage, never from wire_extent.
   * BOOL records 8 bits. FIXED BYTES records zero scalar bits and its exact
   * wire_extent. VAR_DATA STRING/BYTES record zero scalar bits and use
   * tail_prefix_bytes for the Binary representation.
   */
  databind_binary_scalar_kind scalar_kind;
  unsigned scalar_bits;

  /* GROUP only: child fixed block repeated after a u16/u16 group header. */
  size_t child_fixed_block_size;

  /* GROUP/VAR_DATA: binary tail length/header prefix width. */
  size_t tail_prefix_bytes;

  unsigned optional_bit;
  unsigned nullable_bit;
  unsigned flags;
} databind_binary_field_layout;

typedef struct databind_binary_type_layout {
  char *type_id;
  int wire_big_endian;

  size_t fixed_block_size;

  /*
   * Binary state is wire layout, not native host offsets:
   * presence starts at byte 0; null state immediately follows presence.
   */
  size_t presence_offset;
  size_t presence_size;
  size_t null_offset;
  size_t null_size;

  databind_binary_field_layout *fields;
  size_t field_count;
} databind_binary_type_layout;

enum {
  DATABIND_BINARY_LAYOUT_DIAGNOSTIC_TEXT_CAPACITY = 160,
  DATABIND_BINARY_LAYOUT_DIAGNOSTIC_FIELD_CAPACITY = 96
};

typedef struct databind_binary_layout_diagnostic {
  char field[DATABIND_BINARY_LAYOUT_DIAGNOSTIC_FIELD_CAPACITY];
  char text[DATABIND_BINARY_LAYOUT_DIAGNOSTIC_TEXT_CAPACITY];
} databind_binary_layout_diagnostic;

/*
 * Build one independently owned Binary layout from typed Contract IR plus a
 * typed Binary format plan. Semantic kinds come only from Contract IR; offsets/state
 * come only from the Binary format plan. The builder never consumes parser/frontend
 * Node trees, native C offsets, CMeta storage/lifecycle, or historical
 * native host/lifecycle metadata.
 */
databind_binary_layout_status databind_binary_layout_build(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *type_name,
    databind_binary_type_layout *out_layout,
    databind_binary_layout_diagnostic *diagnostic);

databind_binary_layout_status databind_binary_layout_validate(
    const databind_binary_type_layout *layout,
    databind_binary_layout_diagnostic *diagnostic);

void databind_binary_layout_destroy(databind_binary_type_layout *layout);

#ifdef __cplusplus
}
#endif

#endif

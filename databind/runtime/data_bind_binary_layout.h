#ifndef DATA_BIND_BINARY_LAYOUT_H
#define DATA_BIND_BINARY_LAYOUT_H

#include "data_bind.h"

#include <cserde/token.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Element flags are mandatory even when they occupy former record padding.
 * Providers from an earlier plan ABI must be regenerated, not adapted. */
enum { DATA_BIND_BINARY_LAYOUT_PLAN_ABI_VERSION = 2u };
enum { DATA_BIND_BINARY_LAYOUT_MAX_DEPTH = 32u };
enum { DATA_BIND_BINARY_GROUP_HEADER_SIZE = 2u * sizeof(uint16_t) };

#ifndef DATA_BIND_BINARY_LAYOUT_MAX_PAYLOAD_BYTES
#define DATA_BIND_BINARY_LAYOUT_MAX_PAYLOAD_BYTES (16u * 1024u * 1024u)
#endif
#ifndef DATA_BIND_BINARY_LAYOUT_MAX_ITEMS
#define DATA_BIND_BINARY_LAYOUT_MAX_ITEMS 65536u
#endif

enum {
  DATA_BIND_BINARY_FIELD_OPTIONAL = 1u << 0,
  DATA_BIND_BINARY_FIELD_NULLABLE = 1u << 1,
  /* Canonical native enum providers emit UINT bits, including signed enums.
   * The validated layout marks this explicitly; ordinary integers retain
   * strict signed/unsigned token admission. */
  DATA_BIND_BINARY_FIELD_ENUM_BITS = 1u << 2
};

typedef enum DataBindBinaryRepresentation {
  DATA_BIND_BINARY_REP_FIXED = 0,
  DATA_BIND_BINARY_REP_VAR_DATA = 1,
  DATA_BIND_BINARY_REP_GROUP = 2,
  DATA_BIND_BINARY_REP_COUNTED = 3,
  DATA_BIND_BINARY_REP_CURSOR_FIXED = 4
} DataBindBinaryRepresentation;

/*
 * One field in an immutable generated Binary wire-layout plan.
 *
 * token_kind is the canonical CSerde semantic token class, not a second
 * DataBind/Binary type enum. FIXED fields admit BOOL/SINT/UINT/FLOAT,
 * BYTES with zero scalar_bits and an exact wire_extent, or MAP_BEGIN with
 * an exact fixed child plan. ARRAY_BEGIN admits an exact fixed sequence via
 * array_plans, with scalar/bytes or fixed MAP elements. Fixed bytes do not have
 * a length prefix or endian
 * conversion; the immutable input span is borrowed until reader close.
 * VAR_DATA fields admit STRING/BYTES and use a uint32 tail length prefix.
 * GROUP fields admit SEQ_BEGIN with a fixed entry plan and a uint16 stride/count
 * header. The wire stride may exceed the known entry extent on input.
 * Enum/flags FIXED fields use SINT/UINT according to canonical underlying
 * CMeta storage semantics.
 */
typedef struct DataBindBinaryFieldPlan {
  size_t size;
  const char *field_name;
  cserde_token_kind token_kind;
  unsigned scalar_bits;
  size_t wire_offset;
  size_t wire_extent;
  unsigned optional_bit;
  unsigned nullable_bit;
  unsigned flags;

  size_t representation;
  size_t tail_prefix_bytes;
} DataBindBinaryFieldPlan;

/* Sequence/value wire facts. FIXED sequences have a nonzero exact count;
 * COUNTED collections have count zero and read their u32 count from the wire.
 * COUNTED MAP fields have u32-length-prefixed STRING keys.
 * MAP elements use the owner's corresponding child_plans entry. Scalar and
 * exact BYTES elements have no child record. */
typedef struct DataBindBinaryArrayPlan {
  size_t size;
  size_t count;
  size_t element_extent;
  cserde_token_kind element_token_kind;
  unsigned element_scalar_bits;
  /* Uses DATA_BIND_BINARY_FIELD_ENUM_BITS only. */
  unsigned element_flags;
} DataBindBinaryArrayPlan;

static inline unsigned data_bind_binary_array_element_flags(
    const DataBindBinaryArrayPlan *array) {
  return array->element_flags;
}

#define DATA_BIND_BINARY_FIELD_PLAN_INIT \
  { sizeof(DataBindBinaryFieldPlan), NULL, CSERDE_UINT, 0u, \
    0u, 0u, 0u, 0u, 0u, DATA_BIND_BINARY_REP_FIXED, 0u }

/*
 * Runtime-safe immutable lowering of compiler BinaryLayoutIR.
 *
 * This record contains Binary representation facts only. It never contains
 * native C offsets, CMeta lifecycle/storage, schema AST nodes, transport
 * framing, or historical typed descriptors.
 */
typedef struct DataBindBinaryLayoutPlan {
  size_t size;
  uint32_t abi_version;
  const char *type_name;
  int wire_big_endian;
  size_t fixed_block_size;
  size_t presence_offset;
  size_t presence_size;
  size_t null_offset;
  size_t null_size;
  const DataBindBinaryFieldPlan *fields;
  size_t field_count;

  /* If non-NULL, this borrowed table has field_count entries: fixed records
   * and record collections have child plans; other entries are NULL. */
  const struct DataBindBinaryLayoutPlan *const *child_plans;
  /* Optional field_count-entry sparse table for fixed arrays and COUNTED values. */
  const DataBindBinaryArrayPlan *const *array_plans;
} DataBindBinaryLayoutPlan;

#define DATA_BIND_BINARY_LAYOUT_PLAN_INIT \
  { sizeof(DataBindBinaryLayoutPlan), DATA_BIND_BINARY_LAYOUT_PLAN_ABI_VERSION, \
    NULL, 0, 0u, 0u, 0u, 0u, 0u, NULL, 0u, NULL, NULL }

static inline const DataBindBinaryArrayPlan *data_bind_binary_array_plan_at(
    const DataBindBinaryLayoutPlan *plan, size_t field_index) {
  return field_index < plan->field_count && plan->array_plans != NULL
             ? plan->array_plans[field_index] : NULL;
}

/*
 * Validate one generated Binary wire-layout plan.
 *
 * FIXED/CURSOR_FIXED scalar/record/array, GROUP fixed records, COUNTED
 * scalar/STRING/BYTES/fixed-record collections and VAR_DATA STRING/BYTES are
 * admitted. COUNTED MAP keys are length-prefixed STRING tokens. Child
 * records contain only FIXED fields and use their own state bitmaps. Cycles,
 * depth beyond MAX_DEPTH and unsupported shapes fail closed. Truncated layout,
 * field and element records are rejected before their contents are read.
 * GROUP consumes
 * one sequence frame plus one entry record frame and has a UINT16_MAX count.
 * A fixed array uses one sequence frame, plus a record frame for MAP elements;
 * count times element_extent must equal the owning field's wire_extent.
 * The plan,
 * field array and reachable child tables must remain immutable and alive until
 * the reader/writer closes; validation does not retain them.
 */
DATA_BIND_API DataBindStatus data_bind_binary_layout_plan_validate(
    const DataBindBinaryLayoutPlan *plan,
    DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_BINARY_LAYOUT_H */

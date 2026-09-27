#ifndef DATA_BIND_BINARY_READER_H
#define DATA_BIND_BINARY_READER_H

#include "data_bind.h"

#include <cserde/reader.h>
#include <cserde/token.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_BINARY_READER_PLAN_ABI_VERSION = 1u };

enum {
  DATA_BIND_BINARY_READER_FIELD_OPTIONAL = 1u << 0,
  DATA_BIND_BINARY_READER_FIELD_NULLABLE = 1u << 1
};

/*
 * One fixed scalar field in an immutable generated Binary reader plan.
 *
 * token_kind is the canonical CSerde semantic token class, not a second
 * DataBind/Binary type enum. The first reader slice admits BOOL/SINT/UINT/FLOAT
 * only. Enum/flags fields use SINT/UINT according to their canonical underlying
 * CMeta storage semantics.
 */
typedef struct DataBindBinaryReaderFieldPlan {
  size_t size;
  const char *field_name;
  cserde_token_kind token_kind;
  unsigned scalar_bits;
  size_t wire_offset;
  size_t wire_extent;
  unsigned optional_bit;
  unsigned nullable_bit;
  unsigned flags;
} DataBindBinaryReaderFieldPlan;

#define DATA_BIND_BINARY_READER_FIELD_PLAN_INIT \
  { sizeof(DataBindBinaryReaderFieldPlan), NULL, CSERDE_UINT, 0u, \
    0u, 0u, 0u, 0u, 0u }

/*
 * Runtime-safe immutable lowering of compiler BinaryLayoutIR.
 *
 * This record contains Binary representation facts only. It never contains
 * native C offsets, CMeta lifecycle/storage, schema AST nodes, transport
 * framing, or historical typed descriptors.
 */
typedef struct DataBindBinaryReaderPlan {
  size_t size;
  uint32_t abi_version;
  const char *type_name;
  int wire_big_endian;
  size_t fixed_block_size;
  size_t presence_offset;
  size_t presence_size;
  size_t null_offset;
  size_t null_size;
  const DataBindBinaryReaderFieldPlan *fields;
  size_t field_count;
} DataBindBinaryReaderPlan;

#define DATA_BIND_BINARY_READER_PLAN_INIT \
  { sizeof(DataBindBinaryReaderPlan), DATA_BIND_BINARY_READER_PLAN_ABI_VERSION, \
    NULL, 0, 0u, 0u, 0u, 0u, 0u, NULL, 0u }

/*
 * Validate one generated flat fixed-scalar Binary reader plan.
 *
 * GROUP, VAR_DATA and compiler SCALAR_NONE facts cannot be represented by this
 * ABI and therefore must be rejected before a plan is generated.
 */
DATA_BIND_API DataBindStatus data_bind_binary_reader_plan_validate(
    const DataBindBinaryReaderPlan *plan,
    DataBindError *error);

/*
 * Open one borrowed Binary payload as a canonical CSerde message reader.
 *
 * The first slice requires payload_bytes == fixed_block_size. The provider
 * borrows payload bytes and allocates only one fixed-size reader lease object;
 * it never copies the payload or performs schema/reflection lookup.
 */
DATA_BIND_API DataBindStatus data_bind_binary_reader_open(
    const DataBindBinaryReaderPlan *plan,
    const void *payload,
    size_t payload_bytes,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error);

DATA_BIND_API void data_bind_binary_reader_close(
    cserde_reader *reader,
    void *owner);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_BINARY_READER_H */

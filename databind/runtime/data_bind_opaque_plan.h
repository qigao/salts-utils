#ifndef DATA_BIND_OPAQUE_PLAN_H
#define DATA_BIND_OPAQUE_PLAN_H

#include "data_bind.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  DATA_BIND_OPAQUE_PLAN_ABI_VERSION = 1u,
  DATA_BIND_OPAQUE_SPAN_ABI_VERSION = 1u
};

enum DataBindOpaqueStateFlags {
  DATA_BIND_OPAQUE_STATE_VALUE = UINT32_C(1) << 0,
  DATA_BIND_OPAQUE_STATE_ABSENT = UINT32_C(1) << 1,
  DATA_BIND_OPAQUE_STATE_NULL = UINT32_C(1) << 2
};

typedef enum DataBindOpaqueState {
  DATA_BIND_OPAQUE_ABSENT = 1,
  DATA_BIND_OPAQUE_NULL = 2,
  DATA_BIND_OPAQUE_VALUE = 3
} DataBindOpaqueState;

typedef enum DataBindOpaqueOwnership {
  DATA_BIND_OPAQUE_BORROWED = 1,
  DATA_BIND_OPAQUE_CALLER_OWNED = 2
} DataBindOpaqueOwnership;

/*
 * Immutable compiled pass-through plan for canonical builtin bytes.
 *
 * This is intentionally not a DataBindFormatProvider and does not parse,
 * tokenize, inspect schema, or infer native memory layout on the hot path.
 */
typedef struct DataBindOpaquePlan {
  size_t size;
  uint32_t abi_version;
  const char *logical_type;
  uint32_t value_states;
  size_t max_bytes;
} DataBindOpaquePlan;

#define DATA_BIND_OPAQUE_PLAN_INIT \
  { sizeof(DataBindOpaquePlan), DATA_BIND_OPAQUE_PLAN_ABI_VERSION, \
    "bytes", DATA_BIND_OPAQUE_STATE_VALUE, 0u }

/* One bounded pass-through publication view. */
typedef struct DataBindOpaqueSpan {
  size_t size;
  uint32_t abi_version;
  DataBindOpaqueState state;
  const unsigned char *data;
  size_t bytes;
  DataBindOpaqueOwnership ownership;
} DataBindOpaqueSpan;

#define DATA_BIND_OPAQUE_SPAN_INIT \
  { sizeof(DataBindOpaqueSpan), DATA_BIND_OPAQUE_SPAN_ABI_VERSION, \
    DATA_BIND_OPAQUE_ABSENT, NULL, 0u, DATA_BIND_OPAQUE_BORROWED }

DATA_BIND_API DataBindStatus data_bind_opaque_plan_validate(
    const DataBindOpaquePlan *plan,
    DataBindError *error);

/*
 * Publish a borrowed span. The caller retains source ownership for the full
 * lifetime of the returned span.
 */
DATA_BIND_API DataBindStatus data_bind_opaque_plan_borrow(
    const DataBindOpaquePlan *plan,
    DataBindOpaqueState state,
    const void *data,
    size_t bytes,
    DataBindOpaqueSpan *out,
    DataBindError *error);

/*
 * Publish a caller-owned copy into explicit bounded destination storage.
 * No allocation is performed. ABSENT/NULL require zero bytes and no payload.
 */
DATA_BIND_API DataBindStatus data_bind_opaque_plan_copy(
    const DataBindOpaquePlan *plan,
    DataBindOpaqueState state,
    const void *data,
    size_t bytes,
    void *destination,
    size_t destination_bytes,
    DataBindOpaqueSpan *out,
    DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_OPAQUE_PLAN_H */

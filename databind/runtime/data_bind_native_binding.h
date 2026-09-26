#ifndef DATA_BIND_NATIVE_BINDING_H
#define DATA_BIND_NATIVE_BINDING_H

#include "data_bind.h"

#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native binding metadata is format-, transport- and Service-neutral.
 *
 * Version 2 preserves the exact layout/value used while these records were
 * historically declared by data_bind_binding_plan.h; only ownership moves.
 */
enum { DATA_BIND_NATIVE_BINDING_ABI_VERSION = 2u };

/** One generated DataBind state bit outside the canonical CMeta value graph. */
typedef struct DataBindNativeStateBinding {
  size_t size;
  const char *field_name;
  size_t byte_offset;
  unsigned bit;
} DataBindNativeStateBinding;

#define DATA_BIND_NATIVE_STATE_BINDING_INIT \
  { sizeof(DataBindNativeStateBinding), NULL, 0u, 0u }

/**
 * Format-neutral native representation of one DataBind IDL record.
 *
 * data is the canonical CMeta native value descriptor.
 * presence and nulls are DataBind-owned state overlays and are intentionally
 * outside the CMeta field graph.
 */
typedef struct DataBindNativeTypeBinding {
  size_t size;
  uint32_t abi_version;
  const char *idl_type_name;
  const cmeta_data_desc *data;
  const DataBindNativeStateBinding *presence;
  size_t presence_count;
  const DataBindNativeStateBinding *nulls;
  size_t null_count;
} DataBindNativeTypeBinding;

#define DATA_BIND_NATIVE_TYPE_BINDING_INIT(TYPE_NAME, DATA) \
  { sizeof(DataBindNativeTypeBinding), DATA_BIND_NATIVE_BINDING_ABI_VERSION, \
    (TYPE_NAME), (DATA), NULL, 0u, NULL, 0u }

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_NATIVE_BINDING_H */

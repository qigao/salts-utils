#ifndef DATA_BIND_NATIVE_DESCRIPTOR_H
#define DATA_BIND_NATIVE_DESCRIPTOR_H

#include "data_bind.h"
#include "data_bind_binding_plan.h"
#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_BIND_NATIVE_DESCRIPTOR_ABI_VERSION 1u

/*
 * Format-neutral native binding descriptor.
 *
 * Native type/member/layout identity is owned by CMeta. DataBind contributes
 * only the logical schema identity required to join a canonical IDL contract
 * to that native graph. Format/wire metadata is deliberately absent: binary,
 * JSON, XML, CSV and other backends must compile their own plans.
 *
 * All pointers are borrowed immutable metadata and must outlive this descriptor.
 */
typedef struct DataBindNativeDescriptor {
  size_t struct_size;
  uint32_t abi_version;
  const char *schema_type;
  const cmeta_data_desc *native_data;

  /* DataBind logical state overlays. These are not CMeta value fields and are
   * not wire-format layout. */
  const DataBindNativeStateBinding *presence;
  size_t presence_count;
  const DataBindNativeStateBinding *nulls;
  size_t null_count;
} DataBindNativeDescriptor;

#define DATA_BIND_NATIVE_DESCRIPTOR_INIT(SCHEMA_TYPE, NATIVE_DATA) \
  { sizeof(DataBindNativeDescriptor), DATA_BIND_NATIVE_DESCRIPTOR_ABI_VERSION, \
    (SCHEMA_TYPE), (NATIVE_DATA), NULL, 0u, NULL, 0u }

/* Validate only the format-neutral native/schema join. No codec or wire-layout
 * validation is performed here. */
DATA_BIND_API DataBindStatus data_bind_native_descriptor_validate(
    const DataBindNativeDescriptor *descriptor, DataBindError *error);


#ifdef __cplusplus
}
#endif

#endif

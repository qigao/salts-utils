#ifndef DATA_BIND_NATIVE_BINDING_H
#define DATA_BIND_NATIVE_BINDING_H

#include "data_bind.h"

#include <cmeta/data.h>
#include <cmeta/function.h>

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
enum { DATA_BIND_NATIVE_EXECUTION_ABI_VERSION = 1u };
enum { DATA_BIND_MESSAGE_NATIVE_ARTIFACT_ABI_VERSION = 2u };

#if defined(_WIN32)
  #define DATA_BIND_NATIVE_CALL __cdecl
#else
  #define DATA_BIND_NATIVE_CALL
#endif

/**
 * Exact generated native invocation bridge.
 *
 * params[i] points at the exact C parameter object expected by the generated
 * adapter. return_storage points at the exact reflected return object. The
 * boolean reports bridge/admission success only; business/native status remains
 * in the reflected return contract.
 *
 * This is descriptive/exact generated execution glue, not a universal dynamic
 * ABI invocation mechanism.
 */
typedef bool (DATA_BIND_NATIVE_CALL *DataBindNativeInvokeFn)(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count);

typedef struct DataBindNativeExecution {
  size_t size;
  uint32_t abi_version;
  const cmeta_function_desc *function;
  const cmeta_function_abi_desc *abi;
  void *context;
  DataBindNativeInvokeFn invoke;
} DataBindNativeExecution;

#define DATA_BIND_NATIVE_EXECUTION_INIT \
  { sizeof(DataBindNativeExecution), DATA_BIND_NATIVE_EXECUTION_ABI_VERSION, \
    NULL, NULL, NULL, NULL }

static inline int
data_bind_native_execution_valid(const DataBindNativeExecution *execution) {
  return execution != NULL &&
         execution->size >= sizeof(*execution) &&
         execution->abi_version == DATA_BIND_NATIVE_EXECUTION_ABI_VERSION &&
         cmeta_function_desc_valid(execution->function) &&
         cmeta_function_abi_desc_valid(execution->abi) &&
         cmeta_function_desc_equal(execution->function,
                                   execution->abi->function) &&
         execution->invoke != NULL;
}

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

/** Resolve one exact generated Message native binding into caller-owned metadata. */
typedef DataBindStatus (*DataBindNativeTypeBindingResolverFn)(
    DataBindNativeTypeBinding *out,
    DataBindError *error);

/**
 * Transport- and Service-neutral generated Message native-binding artifact.
 *
 * type_name and native_binding have generated/static lifetime. The resolver
 * performs no allocation and returns the canonical CMeta graph plus DataBind
 * presence/null overlays for the exact IDL Message. The record requires the
 * exact current size and ABI; regenerated artifacts replace older providers.
 */
typedef struct DataBindMessageNativeArtifact {
  size_t size;
  uint32_t abi_version;
  const char *type_name;
  DataBindNativeTypeBindingResolverFn native_binding;
} DataBindMessageNativeArtifact;

#define DATA_BIND_MESSAGE_NATIVE_ARTIFACT_INIT \
  { sizeof(DataBindMessageNativeArtifact), \
    DATA_BIND_MESSAGE_NATIVE_ARTIFACT_ABI_VERSION, NULL, NULL }

static inline int
data_bind_message_native_artifact_valid(
    const DataBindMessageNativeArtifact *artifact) {
  return artifact != NULL &&
         artifact->size == sizeof(*artifact) &&
         artifact->abi_version == DATA_BIND_MESSAGE_NATIVE_ARTIFACT_ABI_VERSION &&
         artifact->type_name != NULL && artifact->type_name[0] != '\0' &&
         artifact->native_binding != NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_NATIVE_BINDING_H */

#ifndef DATA_BIND_NATIVE_H
#define DATA_BIND_NATIVE_H

#include "data_bind.h"

#include <cserde/cserde.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_NATIVE_ABI_VERSION = 1u };

typedef struct DataBindNativeOptions {
  size_t size;
  uint32_t abi_version;
  void *workspace;
  size_t workspace_bytes;
  size_t max_depth;
  size_t max_items;
  size_t max_owned_bytes;
} DataBindNativeOptions;

typedef struct DataBindNativeDiagnostic {
  size_t size;
  uint32_t abi_version;
  DataBindError error;
  cserde_status source_status;
} DataBindNativeDiagnostic;

#define DATA_BIND_NATIVE_OPTIONS_INIT                                                   \
  {                                                                                    \
    sizeof(DataBindNativeOptions), DATA_BIND_NATIVE_ABI_VERSION, NULL, 0u, 0u, 0u, 0u \
  }

#define DATA_BIND_NATIVE_DIAGNOSTIC_INIT                                                \
  {                                                                                    \
    sizeof(DataBindNativeDiagnostic), DATA_BIND_NATIVE_ABI_VERSION,                     \
        DATA_BIND_ERROR_INIT, CSERDE_OK                                                 \
  }

/** Measured requirements for the existing native v1 policy, not ORM limits. */
typedef struct DataBindNativeRequirements {
  size_t size;
  uint32_t abi_version;
  size_t workspace_alignment;
  size_t lifecycle_bytes;
  size_t decode_bytes;
  size_t staging_bytes;
  size_t traversal_bytes;
  size_t field_tracking_bytes;
  size_t descriptor_depth;
  size_t descriptor_nodes;
} DataBindNativeRequirements;

#define DATA_BIND_NATIVE_REQUIREMENTS_INIT                                      \
  { sizeof(DataBindNativeRequirements), DATA_BIND_NATIVE_ABI_VERSION,             \
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }

/**
 * Return enough probe bytes for max_depth descriptor pointers at any address.
 * Zero depth or size overflow returns LIMIT. A null result returns INVALID_ARG.
 * The result is unchanged on failure. This function allocates nothing.
 */
DATA_BIND_API DataBindStatus data_bind_native_probe_workspace_size(
    size_t max_depth, size_t *out_bytes);

/**
 * Validate the same canonical graph as native init/decode and measure its
 * workspace, without a reader, destination, provider lifecycle or allocation.
 *
 * options.workspace supplies only traversal storage; use probe_workspace_size
 * to allocate it without knowing binder internals. It may be unaligned. No
 * storage is retained, and requirements is published only after success.
 * Control records and immutable descriptor storage must be disjoint from this
 * mutable probe workspace and from the requirements/diagnostic outputs.
 *
 * Returned lifecycle_bytes and decode_bytes are exact for a workspace base
 * aligned to workspace_alignment (including root staging, traversal and the
 * peak simultaneously active field tracking). For another base, reserve up to
 * workspace_alignment - 1 extra bytes and align it before calling native APIs.
 * Bounds, graph and ABI must remain unchanged when using the result. Provider
 * payload allocations and the bounded recursive C call stack are not included.
 *
 * This does not translate caller-defined scratch/container/per-value budgets:
 * v1 max_depth includes scalar descriptor leaves; max_items is a whole-graph
 * node budget; max_owned_bytes is aggregate logical payload, not heap capacity.
 * Zero depth/items and arithmetic overflow return LIMIT; unsupported or invalid
 * graphs return SCHEMA; invalid records/ranges return INVALID_ARG. Insufficient
 * probe workspace returns LIMIT. No provider callback or source I/O is entered.
 */
DATA_BIND_API DataBindStatus data_bind_native_measure(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    DataBindNativeRequirements *requirements,
    DataBindNativeDiagnostic *diagnostic);

/**
 * Initialize raw native storage to the descriptor-defined semantic-zero state.
 *
 * The complete plain-CMeta graph is validated before mutation. Workspace is
 * borrowed only for bounded graph traversal and retained nowhere.
 */
DATA_BIND_API DataBindStatus data_bind_native_init(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);

/**
 * Restore a live native value to the descriptor-defined semantic-zero state.
 *
 * The complete plain-CMeta graph is validated before cleanup. Provider-owned
 * state is released through its canonical CMeta lifecycle; parent Struct
 * storage is never blanket-zeroed after provider restoration.
 */
DATA_BIND_API DataBindStatus data_bind_native_clear(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);

/**
 * Decode exactly one CSerde value into canonical CMeta native storage.
 *
 * The destination must already be in the descriptor-defined semantic-zero
 * state. The call never owns reader, descriptor, workspace, or destination.
 * All temporary native storage comes from the caller workspace. On failure,
 * destination remains unchanged; on success, one complete value is published
 * and the reader is not probed for a following value or EOF.
 */
DATA_BIND_API DataBindStatus data_bind_native_decode(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    cserde_reader *reader, void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_NATIVE_H */

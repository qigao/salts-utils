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

/** Required caller-owned bookkeeping for the current native reader ABI. */
typedef struct DataBindNativeWorkspaceRequirements {
  size_t size;
  uint32_t abi_version;
  size_t workspace_bytes;
  size_t workspace_alignment;
  size_t traversal_bytes;
  size_t staging_bytes;
  size_t field_tracking_bytes;
  size_t descriptor_depth;
  size_t descriptor_nodes;
} DataBindNativeWorkspaceRequirements;

#define DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT                                    \
  { sizeof(DataBindNativeWorkspaceRequirements), DATA_BIND_NATIVE_ABI_VERSION,            \
    0u, 0u, 0u, 0u, 0u, 0u, 0u }

/**
 * Validate the same canonical graph as init/decode and measure its workspace.
 *
 * options.workspace and options.workspace_bytes are ignored. No allocation,
 * provider lifecycle, reader callback or destination mutation occurs. Traversal
 * is bounded by options.max_depth/max_items and the diagnostic path capacity.
 * Existing native depth/node/owned-payload meanings are not translated here.
 *
 * workspace_bytes is sufficient for init, clear and decode when the workspace
 * address is a multiple of workspace_alignment. staging_bytes includes the
 * native root and padding before the traversal array; field_tracking_bytes is
 * the peak simultaneously active Struct tracking storage. Their sum with
 * traversal_bytes equals workspace_bytes. Owned provider payload/allocator
 * overhead and the decoder's C call stack are not part of this workspace.
 *
 * All arithmetic is overflow checked. Requirements are written only on success;
 * failure leaves them unchanged. Initialize their ABI header with the macro.
 * The shape graph and options must remain unchanged through subsequent calls.
 */
DATA_BIND_API DataBindStatus data_bind_native_workspace_requirements(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    DataBindNativeWorkspaceRequirements *requirements,
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

#ifndef DATA_BIND_NATIVE_H
#define DATA_BIND_NATIVE_H

#include "data_bind.h"

#include <cserde/cserde.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_NATIVE_ABI_VERSION = 2u };
enum { DATA_BIND_NATIVE_PLAN_ABI_VERSION = 1u };

typedef struct DataBindNativePlan DataBindNativePlan;

/** Control records require their exact current size and ABI; use the INIT macros. */
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
  /** CSerde reader or writer status associated with the failure. */
  cserde_status endpoint_status;
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

/** Measured requirements for native storage admission, not ORM limits. */
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
  size_t container_depth;
} DataBindNativeRequirements;

#define DATA_BIND_NATIVE_REQUIREMENTS_INIT                                      \
  { sizeof(DataBindNativeRequirements), DATA_BIND_NATIVE_ABI_VERSION,             \
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }

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
 * peak simultaneously active Struct bitmaps plus collection/map element,
 * key and value temporaries). For another base, reserve up to
 * workspace_alignment - 1 extra bytes and align it before calling native APIs.
 * Bounds, graph and ABI must remain unchanged when using the result. CSTL or
 * other provider-owned container payload allocations and the bounded recursive
 * C call stack are not included; all DataBind temporary native values are.
 *
 * container_depth records the deepest aggregate node (Struct, SEQUENCE, SET or
 * MAP; zero for scalar/enum/buffer roots). field_tracking_bytes counts only
 * active Struct field bitmaps; container temporaries are included in
 * decode_bytes. The requirements
 * record requires the exact current size and ABI; callers must use its INIT.
 * This does not translate caller-defined scratch/container/per-value budgets:
 * max_depth includes scalar descriptor leaves; max_items is a whole-graph
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
 * Compile one immutable native admission plan from a canonical CMeta graph.
 *
 * Compilation performs the same graph admission and workspace measurement as
 * data_bind_native_measure(). The plan borrows shape and every descriptor,
 * type and provider reachable from it; those objects must remain immutable and
 * live until data_bind_native_plan_free().
 *
 * options.workspace is compile-time scratch only and is not retained.
 * max_depth/max_items are admission bounds. max_owned_bytes is not captured:
 * execution keeps using the caller's DataBindNativeOptions payload budget.
 *
 * Runtime plan APIs compare max_depth/max_items against cached measured
 * descriptor requirements and therefore fail before source I/O when a caller
 * supplies a smaller budget. They do not rerun descriptor graph preflight.
 */
DATA_BIND_API DataBindStatus data_bind_native_plan_compile(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    DataBindNativePlan **out_plan,
    DataBindNativeDiagnostic *diagnostic);

DATA_BIND_API void data_bind_native_plan_free(DataBindNativePlan *plan);

/** Borrowed immutable root descriptor retained by the plan. */
DATA_BIND_API const cmeta_data_desc *
data_bind_native_plan_data(const DataBindNativePlan *plan);

/** Borrowed immutable measured requirements retained by the plan. */
DATA_BIND_API const DataBindNativeRequirements *
data_bind_native_plan_requirements(const DataBindNativePlan *plan);

/**
 * Initialize storage through an already-admitted native plan.
 *
 * No descriptor admission or schema lookup occurs. Runtime max_depth/max_items
 * are checked against cached plan requirements before mutation.
 */
DATA_BIND_API DataBindStatus data_bind_native_plan_init(
    const DataBindNativePlan *plan, const DataBindNativeOptions *options,
    void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);

/** Restore one live native value through an already-admitted plan. */
DATA_BIND_API DataBindStatus data_bind_native_plan_clear(
    const DataBindNativePlan *plan, const DataBindNativeOptions *options,
    void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);

/**
 * Decode one value through an already-admitted plan.
 *
 * Runtime allocates nothing. options.workspace owns temporary root/container
 * scratch and max_owned_bytes remains the aggregate payload bound. The plan
 * avoids native descriptor preflight/measurement on this hot path.
 */
DATA_BIND_API DataBindStatus data_bind_native_plan_decode(
    const DataBindNativePlan *plan, const DataBindNativeOptions *options,
    cserde_reader *reader, void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);

DATA_BIND_API DataBindStatus data_bind_native_plan_decode_bounded(
    const DataBindNativePlan *plan, const DataBindNativeOptions *options,
    cserde_reader *reader, void *destination, size_t destination_bytes,
    size_t max_buffer_bytes, DataBindNativeDiagnostic *diagnostic);

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
 *
 * Canonical flags domains accept integer/name tokens and arrays combining
 * them, including nested or empty arrays. Array and element nodes count toward
 * max_items; their depth counts toward max_depth. Assignment occurs only after
 * the complete flags value has been consumed and validated by the CMeta domain.
 * Ordinary enum domains continue to reject array input.
 */
DATA_BIND_API DataBindStatus data_bind_native_decode(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    cserde_reader *reader, void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);

/** Same native decoder with an additional per-owned-value payload limit.
 * Both the aggregate max_owned_bytes and max_buffer_bytes are enforced before
 * provider assignment. Zero permits empty values only. No buffer can borrow
 * source storage; rollback, one-value consumption and ABI checks are unchanged.
 */
DATA_BIND_API DataBindStatus data_bind_native_decode_bounded(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    cserde_reader *reader, void *destination, size_t destination_bytes,
    size_t max_buffer_bytes, DataBindNativeDiagnostic *diagnostic);

/**
 * Encode exactly one canonical native CMeta value to a CSerde writer.
 *
 * The complete descriptor graph is validated before the first writer token.
 * source is borrowed and never mutated. The writer remains caller-owned and is
 * not finished by this function, allowing callers to compose outer framing or
 * transactional provider semantics around the emitted value.
 *
 * max_depth/max_items/max_owned_bytes use the same policy as native decode:
 * max_items bounds semantic descriptor/value nodes; max_owned_bytes bounds the
 * aggregate STRING/BYTES payload exposed through canonical CMeta buffer reads.
 *
 * Writer/provider failure may leave already-emitted tokens in the writer; this
 * function does not claim transport-level transactionality. BindingPlan
 * begin/write/commit/abort remains the publication transaction boundary.
 */
DATA_BIND_API DataBindStatus data_bind_native_encode(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    const void *source, size_t source_bytes, cserde_writer *writer,
    DataBindNativeDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_NATIVE_H */

#ifndef SALTS_PLUGIN_H
#define SALTS_PLUGIN_H

#include <cmeta/cmeta.h>
#include <cmeta/function.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_PLUGIN_ABI_VERSION 1u
#define SALTS_PLUGIN_EXPORT_ABI_VERSION 1u
#define SALTS_PLUGIN_FUNCTION_EXPORT_ABI_VERSION 1u
#define SALTS_PLUGIN_QUERY_SYMBOL "salts_plugin_query"

#define SALTS_PLUGIN_MAX_EXPORTS 256u
#define SALTS_PLUGIN_ID_MAX 255u
#define SALTS_PLUGIN_EXPORT_ID_MAX 255u
#define SALTS_PLUGIN_CONTRACT_ID_MAX 255u
#define SALTS_PLUGIN_MAX_INTERFACE_METHODS 64u
#define SALTS_PLUGIN_INTERFACE_TOKEN_MAX 127u
#define SALTS_PLUGIN_PATH_MAX 4095u
#define SALTS_PLUGIN_MAX_LEASES_PER_PLUGIN 64u

#if defined(__cplusplus)
#  define SALTS_PLUGIN_EXTERN_C extern "C"
#else
#  define SALTS_PLUGIN_EXTERN_C
#endif

#if defined(_WIN32)
#  define SALTS_PLUGIN_ENTRY __declspec(dllexport)
#  define SALTS_PLUGIN_CALL __cdecl
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define SALTS_PLUGIN_ENTRY __attribute__((visibility("default")))
#  define SALTS_PLUGIN_CALL
#else
#  define SALTS_PLUGIN_ENTRY
#  define SALTS_PLUGIN_CALL
#endif

/* Define the one well-known DSO query entry with C linkage even when the
 * plugin implementation is compiled as C++. */
#define SALTS_PLUGIN_QUERY_EXPORT SALTS_PLUGIN_EXTERN_C SALTS_PLUGIN_ENTRY

typedef enum salts_plugin_status {
    SALTS_PLUGIN_OK = 0,
    SALTS_PLUGIN_INVALID_ARGUMENT,
    SALTS_PLUGIN_INVALID_MANIFEST,
    SALTS_PLUGIN_UNSUPPORTED_ABI,
    SALTS_PLUGIN_DUPLICATE_PLUGIN_ID,
    SALTS_PLUGIN_DUPLICATE_EXPORT,
    SALTS_PLUGIN_UNKNOWN_EXPORT,
    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT,
    SALTS_PLUGIN_CAPACITY_EXCEEDED,
    SALTS_PLUGIN_ALLOCATION_FAILED,
    SALTS_PLUGIN_LOAD_FAILED,
    SALTS_PLUGIN_QUERY_MISSING,
    SALTS_PLUGIN_QUERY_REJECTED,
    SALTS_PLUGIN_UNKNOWN_PLUGIN,
    SALTS_PLUGIN_STALE,
    SALTS_PLUGIN_LIFECYCLE_UNSUPPORTED,
    SALTS_PLUGIN_UNLOAD_FAILED,
    SALTS_PLUGIN_ALREADY,
    SALTS_PLUGIN_BUSY,
    SALTS_PLUGIN_INVALID_STATE
} salts_plugin_status;

typedef enum salts_plugin_export_kind {
    SALTS_PLUGIN_EXPORT_INTERFACE = 1,
    SALTS_PLUGIN_EXPORT_CALLABLE = 2
} salts_plugin_export_kind;

typedef struct salts_plugin_version {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} salts_plugin_version;

typedef salts_plugin_status (SALTS_PLUGIN_CALL *salts_plugin_start_fn)(void *self);
typedef salts_plugin_status (SALTS_PLUGIN_CALL *salts_plugin_request_stop_fn)(void *self);
typedef bool (SALTS_PLUGIN_CALL *salts_plugin_is_quiescent_fn)(const void *self);
typedef void (SALTS_PLUGIN_CALL *salts_plugin_destroy_fn)(void *self);
typedef void (SALTS_PLUGIN_CALL *salts_plugin_function_entry)(void);

/*
 * One immutable semantic export row.
 *
 * export_id is unique within one manifest. contract_id + contract_version are
 * the authoritative cross-DSO semantic identity asserted by the domain
 * contract. Any ABI-significant contract change, including method parameter
 * types, requires a contract_version change.
 *
 * interface_desc/interface_value and callable are borrowed representations
 * owned by the loaded plugin and are never pointer identities. interface_value
 * is a mutable borrowed CMeta interface handle because normal interface methods
 * and D0 destruction may update the handle/state; the export row itself remains
 * immutable. Current CMeta
 * interface metadata records name, return spelling and arity, but not parameter
 * type spellings; descriptor comparison is therefore a structural consistency
 * check, not a substitute for contract_id/version discipline.
 */
typedef struct salts_plugin_export {
    uint32_t struct_size;
    uint32_t abi_version;
    salts_plugin_export_kind kind;
    uint32_t contract_version;
    uint64_t capabilities;
    const char *export_id;
    const char *contract_id;

    /* INTERFACE: both are required and callable must be NULL. interface_value
     * points at the concrete CMeta interface value (for example ImageCodec). */
    const cmeta_interface_desc *interface_desc;
    void *interface_value;

    /* CALLABLE: required and interface fields must be NULL. */
    const cmeta_callable *callable;
} salts_plugin_export;

/*
 * Function publication is separate from the frozen V1 export array. The row
 * wraps canonical CMeta reflection/ABI metadata plus a borrowed function-pointer
 * carrier. Plugin validates and publishes this representation but never invokes
 * it generically. Generated exact-ABI consumers convert function_entry back to
 * the exact function type only after contract and CMeta admission.
 */
typedef struct salts_plugin_function_export {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t contract_version;
    uint64_t capabilities;
    const char *export_id;
    const char *contract_id;
    const cmeta_function_desc *function;
    const cmeta_function_abi_desc *function_abi;
    salts_plugin_function_entry function_entry;
} salts_plugin_function_export;

/*
 * Immutable plugin manifest returned by SALTS_PLUGIN_QUERY_SYMBOL.
 *
 * Manifest strings, export rows, interface values, descriptors and callables
 * remain borrowed from the plugin DSO for the entire loaded lifetime. Loader
 * and registry work must not retain them after quiescent unload.
 */
typedef struct salts_plugin_manifest {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *plugin_id;
    salts_plugin_version version;
    uint64_t capabilities;
    const salts_plugin_export *exports;
    size_t export_count;

    /* Optional lifecycle surface. It is all-or-none: passive plugins set self
     * and all callbacks to NULL; managed plugins provide self plus all four
     * callbacks. ABI validation never invokes them. */
    void *self;
    salts_plugin_start_fn start;
    salts_plugin_request_stop_fn request_stop;
    salts_plugin_is_quiescent_fn is_quiescent;
    salts_plugin_destroy_fn destroy;

    /* Optional V2 tail. Function rows are referenced through a pointer array so
     * each pointed-to row can remain independently size-versioned without ever
     * changing array stride for old binaries. Read only when struct_size reaches
     * SALTS_PLUGIN_MANIFEST_V2_SIZE. */
    const salts_plugin_function_export *const *function_exports;
    size_t function_export_count;
} salts_plugin_manifest;

/*
 * V1 minimum readable prefixes. These markers intentionally end at the last
 * V1 field rather than using sizeof(struct), so a future tail extension can
 * keep validating V1 prefixes without silently changing the V1 contract.
 */
#define SALTS_PLUGIN_EXPORT_V1_SIZE \
    ((uint32_t)(offsetof(salts_plugin_export, callable) + \
                sizeof(((salts_plugin_export *)0)->callable)))
#define SALTS_PLUGIN_MANIFEST_V1_SIZE \
    ((uint32_t)(offsetof(salts_plugin_manifest, destroy) + \
                sizeof(((salts_plugin_manifest *)0)->destroy)))
#define SALTS_PLUGIN_FUNCTION_EXPORT_V1_SIZE \
    ((uint32_t)(offsetof(salts_plugin_function_export, function_entry) + \
                sizeof(((salts_plugin_function_export *)0)->function_entry)))
#define SALTS_PLUGIN_MANIFEST_V2_SIZE \
    ((uint32_t)(offsetof(salts_plugin_manifest, function_export_count) + \
                sizeof(((salts_plugin_manifest *)0)->function_export_count)))

typedef const salts_plugin_manifest *(SALTS_PLUGIN_CALL *salts_plugin_query_fn)(
    uint32_t host_abi);

/* Stable public registry reference. slot is 1-based; zero fields are invalid.
 * generation is reserved now so lifecycle/unload can make stale references
 * explicit without changing this ABI later. */
typedef struct salts_plugin_ref {
    uint32_t slot;
    uint32_t generation;
} salts_plugin_ref;

typedef enum salts_plugin_lifecycle_state {
    SALTS_PLUGIN_LIFECYCLE_LOADED = 1,
    SALTS_PLUGIN_LIFECYCLE_STARTING,
    SALTS_PLUGIN_LIFECYCLE_STARTED,
    SALTS_PLUGIN_LIFECYCLE_STOPPING,
    SALTS_PLUGIN_LIFECYCLE_QUIESCENT
} salts_plugin_lifecycle_state;

typedef struct salts_plugin_lease {
    salts_plugin_ref plugin;
    uint32_t slot;
    uint32_t generation;
} salts_plugin_lease;

typedef struct salts_plugin_lifecycle_info {
    salts_plugin_lifecycle_state state;
    size_t active_leases;
    size_t callbacks_inflight;
    salts_plugin_status failure;
} salts_plugin_lifecycle_info;

typedef struct salts_plugin_registry_config {
    size_t capacity;
} salts_plugin_registry_config;

typedef struct salts_plugin_registry {
    void *impl;
} salts_plugin_registry;

static inline bool salts_plugin_ref_valid(salts_plugin_ref ref) {
    return ref.slot != 0u && ref.generation != 0u;
}

static inline bool salts_plugin_lease_valid(salts_plugin_lease lease) {
    return salts_plugin_ref_valid(lease.plugin) &&
           lease.slot != 0u && lease.generation != 0u;
}

const char *salts_plugin_status_string(salts_plugin_status status);

bool salts_plugin_interface_desc_valid(const cmeta_interface_desc *desc);
bool salts_plugin_interface_desc_equal(const cmeta_interface_desc *left,
                                       const cmeta_interface_desc *right);

bool salts_plugin_callable_contract_equal(const cmeta_callable *left,
                                          const cmeta_callable *right);

bool salts_plugin_export_contract_equal(const salts_plugin_export *left,
                                        const salts_plugin_export *right);
bool salts_plugin_export_has_capabilities(const salts_plugin_export *entry,
                                          uint64_t required);

salts_plugin_status salts_plugin_export_require_interface(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_interface_desc *expected_interface);

salts_plugin_status salts_plugin_export_require_callable(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_callable *expected_callable);

salts_plugin_status salts_plugin_function_export_require(
    const salts_plugin_function_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_function_desc **out_function,
    const cmeta_function_abi_desc **out_abi,
    salts_plugin_function_entry *out_entry);

salts_plugin_status salts_plugin_manifest_validate(
    const salts_plugin_manifest *manifest,
    uint32_t host_abi);

salts_plugin_status salts_plugin_manifest_find_export(
    const salts_plugin_manifest *manifest,
    const char *export_id,
    const salts_plugin_export **out_export);

salts_plugin_status salts_plugin_manifest_find_function_export(
    const salts_plugin_manifest *manifest,
    const char *export_id,
    const salts_plugin_function_export **out_export);

/*
 * Bounded dynamic-plugin registry and lifecycle.
 *
 * The registry allocates a fixed slot table once. Paths are borrowed only for
 * one load call, must be strict non-empty UTF-8, and are never retained.
 * Platform library handles remain private.
 *
 * load() publishes state LOADED. start() transitions to STARTED. Managed
 * start() failure is failure-atomic and transitions directly to QUIESCENT so
 * cleanup remains possible. Plugin-owned Interface/Callable/Function/manifest
 * pointers may be used only while holding an explicit lease acquired from a STARTED
 * plugin. request_stop() atomically closes new lease admission before invoking
 * the plugin stop callback. A stop callback failure is recorded as the first
 * lifecycle failure but the state remains STOPPING so quiescence can still be
 * observed and the DSO can still be unloaded safely.
 *
 * poll_quiescent() reaches QUIESCENT only after host leases/in-flight lifecycle
 * callbacks are zero and the optional plugin is_quiescent callback agrees.
 * unload() is allowed only for never-started LOADED plugins or QUIESCENT
 * plugins. Successful unload invalidates the generation-bearing ref.
 *
 * Registry operations are internally synchronized. destroy() is a final
 * control-plane operation and must not race new API calls.
 */
salts_plugin_status salts_plugin_registry_init(
    salts_plugin_registry *registry,
    const salts_plugin_registry_config *config);

salts_plugin_status salts_plugin_registry_load(
    salts_plugin_registry *registry,
    const char *path,
    salts_plugin_ref *out_ref);

salts_plugin_status salts_plugin_registry_find(
    const salts_plugin_registry *registry,
    const char *plugin_id,
    salts_plugin_ref *out_ref);

salts_plugin_status salts_plugin_registry_start(
    salts_plugin_registry *registry,
    salts_plugin_ref ref);

salts_plugin_status salts_plugin_registry_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref ref,
    salts_plugin_lease *out_lease,
    const salts_plugin_manifest **out_manifest);

salts_plugin_status salts_plugin_registry_release(
    salts_plugin_registry *registry,
    salts_plugin_lease *lease);

salts_plugin_status salts_plugin_registry_request_stop(
    salts_plugin_registry *registry,
    salts_plugin_ref ref);

salts_plugin_status salts_plugin_registry_poll_quiescent(
    salts_plugin_registry *registry,
    salts_plugin_ref ref,
    bool *out_quiescent);

salts_plugin_status salts_plugin_registry_get_lifecycle(
    const salts_plugin_registry *registry,
    salts_plugin_ref ref,
    salts_plugin_lifecycle_info *out_info);

salts_plugin_status salts_plugin_registry_unload(
    salts_plugin_registry *registry,
    salts_plugin_ref ref);

size_t salts_plugin_registry_count(const salts_plugin_registry *registry);

salts_plugin_status salts_plugin_registry_destroy(
    salts_plugin_registry *registry);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLUGIN_H */

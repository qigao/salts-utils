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

#define SALTS_PLUGIN_ABI_VERSION 2u
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

typedef uint32_t salts_plugin_export_kind;
enum {
    SALTS_PLUGIN_EXPORT_FUNCTION = 1u,
    SALTS_PLUGIN_EXPORT_INTERFACE = 2u
};

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
 * Plugin ABI 2 is a deliberate breaking cutover.
 *
 * Every export is published through one pointer table. The common row contains
 * Plugin-owned publication identity only. Concrete rows compose CMeta semantics.
 * There is no legacy CALLABLE export and no compatibility/fallback decoding.
 */
typedef struct salts_plugin_export {
    uint32_t struct_size;       /* exact concrete row size */
    uint32_t abi_version;       /* must equal SALTS_PLUGIN_ABI_VERSION */
    salts_plugin_export_kind kind;
    uint32_t contract_version;
    uint64_t capabilities;
    const char *export_id;
    const char *contract_id;
} salts_plugin_export;

typedef struct salts_plugin_function_export {
    salts_plugin_export base;
    const cmeta_function_desc *function;
    const cmeta_function_abi_desc *function_abi;
    salts_plugin_function_entry entry;
} salts_plugin_function_export;

typedef struct salts_plugin_interface_export {
    salts_plugin_export base;
    const cmeta_interface_desc *interface_desc;
    void *interface_value;
} salts_plugin_interface_export;

typedef struct salts_plugin_manifest {
    uint32_t struct_size;       /* must equal sizeof(salts_plugin_manifest) */
    uint32_t abi_version;       /* must equal SALTS_PLUGIN_ABI_VERSION */
    const char *plugin_id;
    salts_plugin_version version;
    uint64_t capabilities;

    /* One flat Plugin publication namespace. Rows are borrowed from the DSO. */
    const salts_plugin_export *const *exports;
    size_t export_count;

    /* Optional lifecycle: passive = all NULL; managed = self + all callbacks. */
    void *self;
    salts_plugin_start_fn start;
    salts_plugin_request_stop_fn request_stop;
    salts_plugin_is_quiescent_fn is_quiescent;
    salts_plugin_destroy_fn destroy;
} salts_plugin_manifest;

#define SALTS_PLUGIN_EXPORT_SIZE ((uint32_t)sizeof(salts_plugin_export))
#define SALTS_PLUGIN_FUNCTION_EXPORT_SIZE     ((uint32_t)sizeof(salts_plugin_function_export))
#define SALTS_PLUGIN_INTERFACE_EXPORT_SIZE     ((uint32_t)sizeof(salts_plugin_interface_export))
#define SALTS_PLUGIN_MANIFEST_SIZE ((uint32_t)sizeof(salts_plugin_manifest))

typedef const salts_plugin_manifest *(SALTS_PLUGIN_CALL *salts_plugin_query_fn)(
    uint32_t host_abi);

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

bool salts_plugin_export_has_capabilities(const salts_plugin_export *entry,
                                          uint64_t required);

salts_plugin_status salts_plugin_export_require_function(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const salts_plugin_function_export **out_export);

salts_plugin_status salts_plugin_export_require_interface(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_interface_desc *expected_interface,
    const salts_plugin_interface_export **out_export);

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

salts_plugin_status salts_plugin_manifest_find_interface_export(
    const salts_plugin_manifest *manifest,
    const char *export_id,
    const salts_plugin_interface_export **out_export);

/*
 * Bounded dynamic-plugin registry and lifecycle.
 *
 * Manifest/export/descriptor/vtable/function-entry pointers are borrowed DSO
 * state and may be dereferenced or invoked only while holding a live lease.
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

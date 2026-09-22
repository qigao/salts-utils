#ifndef SALTS_PLUGIN_H
#define SALTS_PLUGIN_H

#include <cmeta/cmeta.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_PLUGIN_ABI_VERSION 1u
#define SALTS_PLUGIN_EXPORT_ABI_VERSION 1u
#define SALTS_PLUGIN_QUERY_SYMBOL "salts_plugin_query"

#define SALTS_PLUGIN_MAX_EXPORTS 256u
#define SALTS_PLUGIN_ID_MAX 255u
#define SALTS_PLUGIN_EXPORT_ID_MAX 255u
#define SALTS_PLUGIN_CONTRACT_ID_MAX 255u
#define SALTS_PLUGIN_MAX_INTERFACE_METHODS 64u
#define SALTS_PLUGIN_INTERFACE_TOKEN_MAX 127u

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

typedef enum salts_plugin_status {
    SALTS_PLUGIN_OK = 0,
    SALTS_PLUGIN_INVALID_ARGUMENT,
    SALTS_PLUGIN_INVALID_MANIFEST,
    SALTS_PLUGIN_UNSUPPORTED_ABI,
    SALTS_PLUGIN_DUPLICATE_PLUGIN_ID,
    SALTS_PLUGIN_DUPLICATE_EXPORT,
    SALTS_PLUGIN_UNKNOWN_EXPORT,
    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT,
    SALTS_PLUGIN_CAPACITY_EXCEEDED
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

typedef salts_plugin_status (*salts_plugin_start_fn)(void *self);
typedef salts_plugin_status (*salts_plugin_request_stop_fn)(void *self);
typedef bool (*salts_plugin_is_quiescent_fn)(const void *self);
typedef void (*salts_plugin_destroy_fn)(void *self);

/*
 * One immutable semantic export row.
 *
 * export_id is unique within one manifest. contract_id + contract_version are
 * the cross-DSO semantic identity. interface_desc/interface_value and callable
 * are borrowed representations owned by the loaded plugin and are never used as
 * semantic identities.
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
    const void *interface_value;

    /* CALLABLE: required and interface fields must be NULL. */
    const cmeta_callable *callable;
} salts_plugin_export;

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

    /* Optional lifecycle surface. The Plugin core does not invoke these during
     * ABI validation. Later lifecycle orchestration owns start/stop/quiescence. */
    void *self;
    salts_plugin_start_fn start;
    salts_plugin_request_stop_fn request_stop;
    salts_plugin_is_quiescent_fn is_quiescent;
    salts_plugin_destroy_fn destroy;
} salts_plugin_manifest;

#define SALTS_PLUGIN_EXPORT_V1_SIZE ((uint32_t)sizeof(salts_plugin_export))
#define SALTS_PLUGIN_MANIFEST_V1_SIZE ((uint32_t)sizeof(salts_plugin_manifest))

typedef const salts_plugin_manifest *(SALTS_PLUGIN_CALL *salts_plugin_query_fn)(
    uint32_t host_abi);

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

salts_plugin_status salts_plugin_manifest_validate(
    const salts_plugin_manifest *manifest,
    uint32_t host_abi);

salts_plugin_status salts_plugin_manifest_find_export(
    const salts_plugin_manifest *manifest,
    const char *export_id,
    const salts_plugin_export **out_export);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLUGIN_H */

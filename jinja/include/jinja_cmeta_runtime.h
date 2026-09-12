#ifndef JINJA_CMETA_RUNTIME_H
#define JINJA_CMETA_RUNTIME_H

#include "jinja_cmeta.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JINJA_CMETA_RUNTIME_CONFIG JINJA_CMETA_RUNTIME_CONFIG;

typedef enum JINJA_CMETA_RESOURCE {
  JINJA_CMETA_RESOURCE_CELLS = 1,
  JINJA_CMETA_RESOURCE_ACTIVATIONS,
  JINJA_CMETA_RESOURCE_VALUES
} JINJA_CMETA_RESOURCE;

#define JINJA_CMETA_DEFAULT_MAX_CELLS ((size_t)262144u)
#define JINJA_CMETA_DEFAULT_MAX_VALUES ((size_t)262144u)

/** Create an owned configuration; NULL on OUT_OF_MEMORY.
 * Defaults: DEFAULT_MAX_CELLS cells, DEFAULT_MAX_NODES activations,
 * and DEFAULT_MAX_VALUES retained collection values.
 * Dictionary keys and values each consume one value slot.
 * Counts include retained objects across imports, inheritance, and macro calls.
 * Single-threaded mutation: do not modify/destroy during a render using it.
 * error may be NULL. Example:
 * JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
 */
JINJA_CMETA_API JINJA_CMETA_RUNTIME_CONFIG *jinja_cmeta_runtime_config_create(
    JINJA_CMETA_ERROR *error);

/** Release configuration after its users finish; NULL is accepted. */
JINJA_CMETA_API void jinja_cmeta_runtime_config_destroy(JINJA_CMETA_RUNTIME_CONFIG *config);

/** Set a quota in object counts. Zero prohibits allocation; it is not a default.
 * NULL config or an unknown resource returns INVALID_ARGUMENT without mutation.
 * Size/address-space overflow is rejected at render admission with CAPACITY.
 * Example: jinja_cmeta_runtime_config_set_limit(config,
 *              JINJA_CMETA_RESOURCE_CELLS, 8192u, &error).
 */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_runtime_config_set_limit(
    JINJA_CMETA_RUNTIME_CONFIG *config, JINJA_CMETA_RESOURCE resource,
    size_t limit, JINJA_CMETA_ERROR *error);

/** Read a quota. NULL arguments or unknown resource return INVALID_ARGUMENT
 * and leave *out_limit unchanged; error may be NULL.
 */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_runtime_config_get_limit(
    const JINJA_CMETA_RUNTIME_CONFIG *config, JINJA_CMETA_RESOURCE resource,
    size_t *out_limit, JINJA_CMETA_ERROR *error);

/** Render with explicit cell/activation/value quotas; other options keep their meaning.
 * config is borrowed during the call and its limits are copied at entry.
 * NULL config uses DEFAULT_MAX_CELLS, DEFAULT_MAX_VALUES, and resolved max_nodes for
 * activations, as the existing render entry points do. Explicit configuration
 * overrides only those three quotas, not node or active-binding admission.
 * Value slots are shared across all templates in one render, not derived from
 * the entry template. This count is not a total retained-memory byte limit.
 * Collection storage is allocated on demand in contiguous snapshot spans;
 * later snapshots do not relocate published values. A context may grow only
 * while being constructed, before any template or closure can borrow it.
 * Inputs and streaming/error contracts are identical to jinja_cmeta_render.
 * Already streamed bytes and host side effects are not rolled back on failure.
 */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_render_ex(
    const JINJA_CMETA_TEMPLATE *templ, const cmeta_data_desc *root_desc, const void *root,
    const JINJA_CMETA_RENDER_OPTIONS *options, const JINJA_CMETA_RUNTIME_CONFIG *config,
    const JINJA_CMETA_RENDERER *renderer, void *renderer_data, JINJA_CMETA_ERROR *error);

/** String variant of render_ex: *out_text is NULL on failure and malloc-owned
 * on success. Caller frees the result. NULL out_text returns INVALID_ARGUMENT.
 * Example: jinja_cmeta_render_string_ex(templ, jinja_cmeta_vstr_data(), &root,
 *              NULL, config, &output, &error).
 */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_render_string_ex(
    const JINJA_CMETA_TEMPLATE *templ, const cmeta_data_desc *root_desc, const void *root,
    const JINJA_CMETA_RENDER_OPTIONS *options, const JINJA_CMETA_RUNTIME_CONFIG *config,
    char **out_text, JINJA_CMETA_ERROR *error);

#ifdef __cplusplus
}
#endif

#endif

#include "jinja_cmeta_runtime_internal.h"
#include "jinja_cmeta_internal.h"

#include <stdlib.h>

JINJA_CMETA_RUNTIME_CONFIG *jinja_cmeta_runtime_config_create(JINJA_CMETA_ERROR *error) {
  jinja_cmeta_error_clear(error);
  JINJA_CMETA_RUNTIME_CONFIG *config = (JINJA_CMETA_RUNTIME_CONFIG *)malloc(sizeof(*config));
  if (config == NULL) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, 0u,
        "unable to allocate runtime configuration");
    return NULL;
  }
  *config = (JINJA_CMETA_RUNTIME_CONFIG){.max_cells = JINJA_CMETA_DEFAULT_MAX_CELLS,
      .max_activations = JINJA_CMETA_DEFAULT_MAX_NODES,
      .max_values = JINJA_CMETA_DEFAULT_MAX_VALUES};
  return config;
}

void jinja_cmeta_runtime_config_destroy(JINJA_CMETA_RUNTIME_CONFIG *config) {
  free(config);
}

JINJA_CMETA_STATUS jinja_cmeta_runtime_config_set_limit(JINJA_CMETA_RUNTIME_CONFIG *config,
    JINJA_CMETA_RESOURCE resource, size_t limit, JINJA_CMETA_ERROR *error) {
  jinja_cmeta_error_clear(error);
  if (config == NULL || (resource != JINJA_CMETA_RESOURCE_CELLS &&
      resource != JINJA_CMETA_RESOURCE_ACTIVATIONS && resource != JINJA_CMETA_RESOURCE_VALUES)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "runtime configuration and a supported resource are required");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  if (resource == JINJA_CMETA_RESOURCE_CELLS) config->max_cells = limit;
  else if (resource == JINJA_CMETA_RESOURCE_ACTIVATIONS) config->max_activations = limit;
  else config->max_values = limit;
  return JINJA_CMETA_OK;
}

JINJA_CMETA_STATUS jinja_cmeta_runtime_config_get_limit(const JINJA_CMETA_RUNTIME_CONFIG *config,
    JINJA_CMETA_RESOURCE resource, size_t *out_limit, JINJA_CMETA_ERROR *error) {
  jinja_cmeta_error_clear(error);
  if (config == NULL || out_limit == NULL || (resource != JINJA_CMETA_RESOURCE_CELLS &&
      resource != JINJA_CMETA_RESOURCE_ACTIVATIONS && resource != JINJA_CMETA_RESOURCE_VALUES)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "runtime configuration, a supported resource, and output are required");
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  }
  if (resource == JINJA_CMETA_RESOURCE_CELLS) *out_limit = config->max_cells;
  else if (resource == JINJA_CMETA_RESOURCE_ACTIVATIONS) *out_limit = config->max_activations;
  else *out_limit = config->max_values;
  return JINJA_CMETA_OK;
}

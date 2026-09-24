#ifndef DATABIND_COMPILER_PROJECTION_CONFIG_H
#define DATABIND_COMPILER_PROJECTION_CONFIG_H

#include "method_plan_projection.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct databind_compiler_projection_config {
  void *json_root;

  databind_compiler_http_operation_config *http_operations;
  databind_compiler_http_field_config *http_fields;
  databind_compiler_http_error_config *http_errors;
  databind_compiler_http_projection_config http;
  int has_http;

  databind_compiler_rpc_operation_config *rpc_operations;
  databind_compiler_rpc_field_config *rpc_fields;
  databind_compiler_rpc_error_config *rpc_errors;
  databind_compiler_rpc_projection_config rpc;
  int has_rpc;
} databind_compiler_projection_config;

/*
 * Parse one compiler/control-plane JSON projection config.
 *
 * The config owns only transport representation. String pointers in the
 * materialized HTTP/RPC configs are borrowed from the retained JSON DOM and
 * remain valid until dispose().
 */
int databind_compiler_projection_config_load(
    const char *path,
    databind_compiler_projection_config *out,
    char *error,
    size_t error_size);

void databind_compiler_projection_config_dispose(
    databind_compiler_projection_config *config);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_PROJECTION_CONFIG_H */

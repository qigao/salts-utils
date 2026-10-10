#ifndef DATABIND_COMPILER_PROJECTION_CONFIG_H
#define DATABIND_COMPILER_PROJECTION_CONFIG_H

#include "method_plan_projection.h"
#include "socket_plan_projection.h"
#include "flowmq_plan_projection.h"

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

  size_t opaque_max_bytes;
  int has_opaque;

  databind_compiler_socket_projection_config socket;
  int has_socket;

  databind_compiler_flowmq_projection_config flowmq;
  int has_flowmq;
} databind_compiler_projection_config;

/*
 * Parse one compiler/control-plane JSON projection config.
 *
 * The config owns only transport representation. String pointers in the
 * materialized HTTP/RPC/Socket/FlowMQ configs are borrowed from the retained JSON DOM
 * and remain valid until dispose().
 */
int databind_compiler_projection_config_load(
    const char *path,
    databind_compiler_projection_config *out,
    char *error,
    size_t error_size);

/* Compiler-private owned DOM entry. Consumes root on success or failure;
 * applies exactly the same validation as the external JSON configuration. */
int databind_compiler_projection_config_from_json(
    void *root, databind_compiler_projection_config *out,
    char *error, size_t error_size);

void databind_compiler_projection_config_dispose(
    databind_compiler_projection_config *config);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_PROJECTION_CONFIG_H */

#ifndef DATABIND_COMPILER_METHOD_PLAN_PROJECTION_H
#define DATABIND_COMPILER_METHOD_PLAN_PROJECTION_H

#include "projection.h"
#include "../runtime/data_bind.h"
#include "../runtime/data_bind_opaque_plan.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum databind_compiler_http_field_location {
  DATABIND_COMPILER_HTTP_PATH = 1,
  DATABIND_COMPILER_HTTP_QUERY,
  DATABIND_COMPILER_HTTP_HEADER,
  DATABIND_COMPILER_HTTP_COOKIE,
  DATABIND_COMPILER_HTTP_BODY,
  DATABIND_COMPILER_HTTP_RESPONSE_HEADER,
  DATABIND_COMPILER_HTTP_RESPONSE_BODY
} databind_compiler_http_field_location;

/* Build-only policy declarations: ordered, unique names, at most 16 per method. */
int databind_compiler_http_policies_valid(const IdlOperation *operation);

typedef enum databind_compiler_projection_direction {
  DATABIND_COMPILER_PROJECTION_INGRESS = 1,
  DATABIND_COMPILER_PROJECTION_EGRESS = 2
} databind_compiler_projection_direction;

typedef struct databind_compiler_http_operation_config {
  const char *service_name;
  const char *operation_name;
  const char *method;
  const char *route;
  int success_status;
  uint64_t context_flags;
  DataBindFormat ingress_format;
  DataBindFormat egress_format;
  DataBindPayloadKind ingress_payload_kind;
  DataBindPayloadKind egress_payload_kind;
  size_t opaque_max_bytes;
} databind_compiler_http_operation_config;

typedef struct databind_compiler_http_field_config {
  const char *service_name;
  const char *operation_name;
  databind_compiler_projection_direction direction;
  const char *schema_field;
  databind_compiler_http_field_location location;
  const char *wire_name;
  size_t ordinal;
} databind_compiler_http_field_config;

typedef struct databind_compiler_http_error_config {
  const char *service_name;
  const char *operation_name;
  const char *error_type;
  int status;
} databind_compiler_http_error_config;

typedef struct databind_compiler_http_projection_config {
  const char *symbol_prefix;
  const databind_compiler_http_operation_config *operations;
  size_t operation_count;
  const databind_compiler_http_field_config *fields;
  size_t field_count;
  const databind_compiler_http_error_config *errors;
  size_t error_count;
} databind_compiler_http_projection_config;

typedef struct databind_compiler_rpc_operation_config {
  const char *service_name;
  const char *operation_name;
  const char *wire_method;
  DataBindFormat ingress_format;
  DataBindFormat egress_format;
} databind_compiler_rpc_operation_config;

typedef struct databind_compiler_rpc_field_config {
  const char *service_name;
  const char *operation_name;
  databind_compiler_projection_direction direction;
  const char *schema_field;
  const char *wire_name;
  size_t ordinal;
} databind_compiler_rpc_field_config;

typedef struct databind_compiler_rpc_error_config {
  const char *service_name;
  const char *operation_name;
  const char *error_type;
  int code;
} databind_compiler_rpc_error_config;

typedef struct databind_compiler_rpc_projection_config {
  const char *symbol_prefix;
  const databind_compiler_rpc_operation_config *operations;
  size_t operation_count;
  const databind_compiler_rpc_field_config *fields;
  size_t field_count;
  const databind_compiler_rpc_error_config *errors;
  size_t error_count;
} databind_compiler_rpc_projection_config;

databind_compiler_projection_backend
databind_compiler_http_method_plan_backend(void);

databind_compiler_projection_backend
databind_compiler_rpc_method_plan_backend(void);

/* Shared schema-aware admission for application annotations before JSON export.
 * The caller supplies named operations from this Contract; no output is written. */
int databind_compiler_method_plan_configs_valid(
    const IdlContract *contract,
    const databind_compiler_http_projection_config *http,
    const databind_compiler_rpc_projection_config *rpc);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_METHOD_PLAN_PROJECTION_H */

#ifndef DATA_BIND_METHOD_PLAN_H
#define DATA_BIND_METHOD_PLAN_H

#include "data_bind_binding_plan.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_METHOD_PLAN_ABI_VERSION = 1u };

typedef enum DataBindHttpFieldLocation {
  DATA_BIND_HTTP_PATH = 1,
  DATA_BIND_HTTP_QUERY,
  DATA_BIND_HTTP_HEADER,
  DATA_BIND_HTTP_COOKIE,
  DATA_BIND_HTTP_BODY,
  DATA_BIND_HTTP_RESPONSE_HEADER,
  DATA_BIND_HTTP_RESPONSE_BODY
} DataBindHttpFieldLocation;

typedef struct DataBindHttpFieldProjection {
  size_t size;
  DataBindBindingDirection direction;
  const char *schema_field;
  DataBindHttpFieldLocation location;
  const char *wire_name;
  size_t ordinal;
} DataBindHttpFieldProjection;

#define DATA_BIND_HTTP_FIELD_PROJECTION_INIT \
  { sizeof(DataBindHttpFieldProjection), 0, NULL, 0, NULL, SIZE_MAX }

typedef struct DataBindHttpErrorMapping {
  size_t size;
  const char *error_type;
  int status;
} DataBindHttpErrorMapping;

#define DATA_BIND_HTTP_ERROR_MAPPING_INIT \
  { sizeof(DataBindHttpErrorMapping), NULL, 500 }

enum DataBindHttpContextFlags {
  DATA_BIND_HTTP_CONTEXT_NONE = 0u,
  DATA_BIND_HTTP_CONTEXT_DEADLINE = UINT64_C(1) << 0,
  DATA_BIND_HTTP_CONTEXT_CANCELLATION = UINT64_C(1) << 1,
  DATA_BIND_HTTP_CONTEXT_REQUEST_ID = UINT64_C(1) << 2,
  DATA_BIND_HTTP_CONTEXT_TRACE = UINT64_C(1) << 3,
  DATA_BIND_HTTP_CONTEXT_PRINCIPAL = UINT64_C(1) << 4,
  DATA_BIND_HTTP_CONTEXT_METADATA = UINT64_C(1) << 5
};

typedef struct DataBindHttpProjectionConfig {
  size_t size;
  uint32_t abi_version;
  const char *method;
  const char *route;
  int success_status;
  uint64_t context_flags;
  const DataBindHttpFieldProjection *fields;
  size_t field_count;
  const DataBindHttpErrorMapping *errors;
  size_t error_count;
} DataBindHttpProjectionConfig;

#define DATA_BIND_HTTP_PROJECTION_CONFIG_INIT \
  { sizeof(DataBindHttpProjectionConfig), DATA_BIND_METHOD_PLAN_ABI_VERSION, \
    NULL, NULL, 200, DATA_BIND_HTTP_CONTEXT_NONE, NULL, 0u, NULL, 0u }

typedef struct DataBindRpcFieldProjection {
  size_t size;
  DataBindBindingDirection direction;
  const char *schema_field;
  const char *wire_name;
  size_t ordinal;
} DataBindRpcFieldProjection;

#define DATA_BIND_RPC_FIELD_PROJECTION_INIT \
  { sizeof(DataBindRpcFieldProjection), 0, NULL, NULL, SIZE_MAX }

typedef struct DataBindRpcErrorMapping {
  size_t size;
  const char *error_type;
  int code;
} DataBindRpcErrorMapping;

#define DATA_BIND_RPC_ERROR_MAPPING_INIT \
  { sizeof(DataBindRpcErrorMapping), NULL, -32000 }

typedef struct DataBindRpcProjectionConfig {
  size_t size;
  uint32_t abi_version;
  const char *wire_method;
  const DataBindRpcFieldProjection *fields;
  size_t field_count;
  const DataBindRpcErrorMapping *errors;
  size_t error_count;
} DataBindRpcProjectionConfig;

#define DATA_BIND_RPC_PROJECTION_CONFIG_INIT \
  { sizeof(DataBindRpcProjectionConfig), DATA_BIND_METHOD_PLAN_ABI_VERSION, \
    NULL, NULL, 0u, NULL, 0u }

typedef struct DataBindHttpProjectionArtifactEntry {
  size_t size;
  const char *service_name;
  const char *operation_name;
  DataBindHttpProjectionConfig config;
} DataBindHttpProjectionArtifactEntry;

#define DATA_BIND_HTTP_PROJECTION_ARTIFACT_ENTRY_INIT \
  { sizeof(DataBindHttpProjectionArtifactEntry), NULL, NULL, \
    DATA_BIND_HTTP_PROJECTION_CONFIG_INIT }

typedef struct DataBindHttpProjectionArtifact {
  size_t size;
  uint32_t abi_version;
  const DataBindHttpProjectionArtifactEntry *entries;
  size_t entry_count;
} DataBindHttpProjectionArtifact;

#define DATA_BIND_HTTP_PROJECTION_ARTIFACT_INIT \
  { sizeof(DataBindHttpProjectionArtifact), DATA_BIND_METHOD_PLAN_ABI_VERSION, \
    NULL, 0u }

typedef struct DataBindRpcProjectionArtifactEntry {
  size_t size;
  const char *service_name;
  const char *operation_name;
  DataBindRpcProjectionConfig config;
} DataBindRpcProjectionArtifactEntry;

#define DATA_BIND_RPC_PROJECTION_ARTIFACT_ENTRY_INIT \
  { sizeof(DataBindRpcProjectionArtifactEntry), NULL, NULL, \
    DATA_BIND_RPC_PROJECTION_CONFIG_INIT }

typedef struct DataBindRpcProjectionArtifact {
  size_t size;
  uint32_t abi_version;
  const DataBindRpcProjectionArtifactEntry *entries;
  size_t entry_count;
} DataBindRpcProjectionArtifact;

#define DATA_BIND_RPC_PROJECTION_ARTIFACT_INIT \
  { sizeof(DataBindRpcProjectionArtifact), DATA_BIND_METHOD_PLAN_ABI_VERSION, \
    NULL, 0u }

DATA_BIND_API const DataBindHttpProjectionConfig *
data_bind_http_projection_artifact_find(
    const DataBindHttpProjectionArtifact *artifact,
    const char *service_name, const char *operation_name);

DATA_BIND_API const DataBindRpcProjectionConfig *
data_bind_rpc_projection_artifact_find(
    const DataBindRpcProjectionArtifact *artifact,
    const char *service_name, const char *operation_name);

typedef struct DataBindHttpMethodPlan DataBindHttpMethodPlan;
typedef struct DataBindRpcMethodPlan DataBindRpcMethodPlan;

DATA_BIND_API DataBindStatus data_bind_http_method_plan_compile_service(
    DataBind *codec,
    const char *service_name,
    const char *operation_name,
    const DataBindHttpProjectionConfig *config,
    const DataBindServiceNativeBinding *native,
    DataBindHttpMethodPlan **out_plan,
    DataBindBindingPlanDiagnostic *diagnostic);

DATA_BIND_API void data_bind_http_method_plan_free(DataBindHttpMethodPlan *plan);

DATA_BIND_API const char *
data_bind_http_method_plan_method(const DataBindHttpMethodPlan *plan);
DATA_BIND_API const char *
data_bind_http_method_plan_route(const DataBindHttpMethodPlan *plan);
DATA_BIND_API int
data_bind_http_method_plan_success_status(const DataBindHttpMethodPlan *plan);
DATA_BIND_API uint64_t
data_bind_http_method_plan_context_flags(const DataBindHttpMethodPlan *plan);
DATA_BIND_API const DataBindBindingPlan *
data_bind_http_method_plan_binding(const DataBindHttpMethodPlan *plan);
DATA_BIND_API size_t
data_bind_http_method_plan_error_count(const DataBindHttpMethodPlan *plan);
DATA_BIND_API int data_bind_http_method_plan_error_at(
    const DataBindHttpMethodPlan *plan, size_t index,
    DataBindHttpErrorMapping *out);

/**
 * Map a completed canonical BindingPlan outcome into the HTTP projection.
 *
 * SUCCESS maps to success_status and TYPED_ERROR maps by canonical error index.
 * NATIVE_STATUS remains a distinct business outcome and is deliberately not
 * assigned an HTTP status by this transport projection.
 */
DATA_BIND_API int data_bind_http_method_plan_status_for_outcome(
    const DataBindHttpMethodPlan *plan,
    const DataBindBindingOutcome *outcome,
    int *out_status);

DATA_BIND_API DataBindStatus data_bind_rpc_method_plan_compile_service(
    DataBind *codec,
    const char *service_name,
    const char *operation_name,
    const DataBindRpcProjectionConfig *config,
    const DataBindServiceNativeBinding *native,
    DataBindRpcMethodPlan **out_plan,
    DataBindBindingPlanDiagnostic *diagnostic);

DATA_BIND_API void data_bind_rpc_method_plan_free(DataBindRpcMethodPlan *plan);

DATA_BIND_API const char *
data_bind_rpc_method_plan_wire_method(const DataBindRpcMethodPlan *plan);
DATA_BIND_API const DataBindBindingPlan *
data_bind_rpc_method_plan_binding(const DataBindRpcMethodPlan *plan);
DATA_BIND_API size_t
data_bind_rpc_method_plan_error_count(const DataBindRpcMethodPlan *plan);
DATA_BIND_API int data_bind_rpc_method_plan_error_at(
    const DataBindRpcMethodPlan *plan, size_t index,
    DataBindRpcErrorMapping *out);

/**
 * Map a completed canonical BindingPlan outcome into the RPC projection.
 *
 * SUCCESS maps to code 0 and TYPED_ERROR maps by canonical error index.
 * NATIVE_STATUS remains a distinct business outcome and is deliberately not
 * assigned an RPC error code by this transport projection.
 */
DATA_BIND_API int data_bind_rpc_method_plan_code_for_outcome(
    const DataBindRpcMethodPlan *plan,
    const DataBindBindingOutcome *outcome,
    int *out_code);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_METHOD_PLAN_H */

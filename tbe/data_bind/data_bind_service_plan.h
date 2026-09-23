#ifndef DATA_BIND_SERVICE_PLAN_H
#define DATA_BIND_SERVICE_PLAN_H

#include "data_bind.h"
#include "data_bind_native.h"
#include "tbe_typed.h"

#include <cmeta/function.h>
#include <cserde/reader.h>

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(DATA_BIND_BUILD_DLL)
#define DATA_BIND_SERVICE_API __declspec(dllexport)
#else
#define DATA_BIND_SERVICE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_SERVICE_PLAN_ABI_VERSION = 1u };

typedef enum DataBindServiceProjection {
  DATA_BIND_SERVICE_PROJECTION_HTTP = 1,
  DATA_BIND_SERVICE_PROJECTION_RPC = 2
} DataBindServiceProjection;

typedef enum DataBindServicePlanDirection {
  DATA_BIND_SERVICE_PLAN_INGRESS = 1,
  DATA_BIND_SERVICE_PLAN_EGRESS = 2
} DataBindServicePlanDirection;

typedef enum DataBindServiceBindingSource {
  DATA_BIND_SERVICE_SOURCE_NONE = 0,
  DATA_BIND_SERVICE_SOURCE_PATH,
  DATA_BIND_SERVICE_SOURCE_QUERY,
  DATA_BIND_SERVICE_SOURCE_HEADER,
  DATA_BIND_SERVICE_SOURCE_COOKIE,
  DATA_BIND_SERVICE_SOURCE_BODY,
  DATA_BIND_SERVICE_SOURCE_RPC_PARAM,
  DATA_BIND_SERVICE_SOURCE_RESULT,
  DATA_BIND_SERVICE_SOURCE_ERROR
} DataBindServiceBindingSource;

typedef enum DataBindServiceBindingTarget {
  DATA_BIND_SERVICE_TARGET_REQUEST_FIELD = 1,
  DATA_BIND_SERVICE_TARGET_FUNCTION_PARAM,
  DATA_BIND_SERVICE_TARGET_RETURN_VALUE
} DataBindServiceBindingTarget;

/**
 * One immutable compiled binding step.
 *
 * Strings are plan-owned. data borrows immutable CMeta provider metadata.
 * function_param_index is SIZE_MAX when no function parameter is involved.
 */
typedef struct DataBindServicePlanEntry {
  size_t size;
  DataBindServicePlanDirection direction;
  DataBindServiceBindingSource source;
  DataBindServiceBindingTarget target;
  const char *schema_field;
  const char *wire_name;
  const char *function_param;
  size_t function_param_index;
  size_t native_offset;
  int parameter_indirect;
  int required;
  int has_default;
  const char *default_value;
  const char *format;
  const cmeta_data_desc *data;
  /** Root-request optional presence metadata, appended to the v1 entry. */
  int has_presence;
  size_t presence_offset;
  unsigned optional_bit;
} DataBindServicePlanEntry;

#define DATA_BIND_SERVICE_PLAN_ENTRY_INIT {sizeof(DataBindServicePlanEntry)}

typedef struct DataBindServiceNativeBinding {
  size_t size;
  uint32_t abi_version;
  const cmeta_function_desc *function;
  const TbeTypedDescriptor *request;
  const TbeTypedDescriptor *response;
} DataBindServiceNativeBinding;

#define DATA_BIND_SERVICE_NATIVE_BINDING_INIT(FUNCTION, REQUEST, RESPONSE) \
  { sizeof(DataBindServiceNativeBinding), DATA_BIND_SERVICE_PLAN_ABI_VERSION, \
    (FUNCTION), (REQUEST), (RESPONSE) }

typedef struct DataBindServicePlanDiagnostic {
  size_t size;
  DataBindStatus status;
  char schema_field[96];
  char function_param[96];
  char message[256];
} DataBindServicePlanDiagnostic;

#define DATA_BIND_SERVICE_PLAN_DIAGNOSTIC_INIT \
  { sizeof(DataBindServicePlanDiagnostic), DATA_BIND_OK, {0}, {0}, {0} }

typedef struct DataBindServiceCallFrame {
  size_t size;
  void *request;
  size_t request_bytes;
  void *return_value;
  size_t return_bytes;
  void *const *params;
  const size_t *param_bytes;
  size_t param_count;
} DataBindServiceCallFrame;

#define DATA_BIND_SERVICE_CALL_FRAME_INIT \
  { sizeof(DataBindServiceCallFrame), NULL, 0u, NULL, 0u, NULL, NULL, 0u }

typedef struct DataBindServicePlan DataBindServicePlan;

/**
 * Input provider callback.
 *
 * On DATA_BIND_OK, present is set to 0 or 1. When present=1, reader must be
 * initialized and remains valid synchronously until the callback caller
 * finishes decoding this entry. Missing values are not themselves errors.
 */
typedef DataBindStatus (*DataBindServiceOpenInputFn)(
    void *context, const DataBindServicePlanEntry *entry,
    cserde_reader *reader, int *present, DataBindError *error);

/**
 * Output callbacks form a transaction. begin_output/commit_output/abort_output
 * are required when a plan has egress entries so a failed multi-field result
 * never publishes a partial transport response.
 */
typedef DataBindStatus (*DataBindServiceBeginOutputFn)(
    void *context, DataBindError *error);
typedef DataBindStatus (*DataBindServiceWriteOutputFn)(
    void *context, const DataBindServicePlanEntry *entry,
    const void *value, size_t value_bytes, DataBindError *error);
typedef DataBindStatus (*DataBindServiceCommitOutputFn)(
    void *context, DataBindError *error);
typedef void (*DataBindServiceAbortOutputFn)(void *context);

typedef struct DataBindServiceProvider {
  size_t size;
  uint32_t abi_version;
  void *context;
  DataBindServiceOpenInputFn open_input;
  DataBindServiceBeginOutputFn begin_output;
  DataBindServiceWriteOutputFn write_output;
  DataBindServiceCommitOutputFn commit_output;
  DataBindServiceAbortOutputFn abort_output;
} DataBindServiceProvider;

#define DATA_BIND_SERVICE_PROVIDER_INIT \
  { sizeof(DataBindServiceProvider), DATA_BIND_SERVICE_PLAN_ABI_VERSION, \
    NULL, NULL, NULL, NULL, NULL, NULL }

/**
 * Compile one immutable transport projection.
 *
 * Compilation resolves all schema attributes, defaults, native offsets and
 * function parameter mappings. The plan does not retain or re-walk schema AST
 * nodes at runtime. request/response descriptors and CMeta function metadata
 * must outlive the plan.
 */
DATA_BIND_SERVICE_API DataBindStatus data_bind_service_plan_compile(
    DataBind *codec, const char *service_name, const char *operation_name,
    DataBindServiceProjection projection,
    const DataBindServiceNativeBinding *native,
    DataBindServicePlan **out_plan,
    DataBindServicePlanDiagnostic *diagnostic);

DATA_BIND_SERVICE_API void data_bind_service_plan_free(DataBindServicePlan *plan);

DATA_BIND_SERVICE_API size_t
data_bind_service_plan_ingress_count(const DataBindServicePlan *plan);
DATA_BIND_SERVICE_API size_t
data_bind_service_plan_egress_count(const DataBindServicePlan *plan);
DATA_BIND_SERVICE_API int data_bind_service_plan_ingress_at(
    const DataBindServicePlan *plan, size_t index,
    DataBindServicePlanEntry *out);
DATA_BIND_SERVICE_API int data_bind_service_plan_egress_at(
    const DataBindServicePlan *plan, size_t index,
    DataBindServicePlanEntry *out);
DATA_BIND_SERVICE_API size_t
data_bind_service_plan_error_count(const DataBindServicePlan *plan);
DATA_BIND_SERVICE_API const char *data_bind_service_plan_error_at(
    const DataBindServicePlan *plan, size_t index);
DATA_BIND_SERVICE_API const cmeta_function_desc *
data_bind_service_plan_function(const DataBindServicePlan *plan);

/**
 * Initialize and bind all ingress storage.
 *
 * Frame destinations are raw storage owned by the caller. This function
 * initializes them to canonical semantic zero before reading provider values.
 * On failure every initialized destination is restored to semantic zero. On
 * success ownership transfers to the caller/exact-ABI adapter, which must
 * later clear managed values through the same native descriptors.
 */
DATA_BIND_SERVICE_API DataBindStatus data_bind_service_plan_bind_inputs(
    const DataBindServicePlan *plan,
    const DataBindServiceProvider *provider,
    const DataBindNativeOptions *native_options,
    DataBindServiceCallFrame *frame,
    DataBindServicePlanDiagnostic *diagnostic);

/**
 * Publish result values through one provider transaction.
 *
 * The plan never invokes the C function. The exact-ABI adapter populates the
 * frame first, then this function projects return/OUT storage to the provider.
 */
DATA_BIND_SERVICE_API DataBindStatus data_bind_service_plan_write_outputs(
    const DataBindServicePlan *plan,
    const DataBindServiceProvider *provider,
    const DataBindServiceCallFrame *frame,
    DataBindServicePlanDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_SERVICE_PLAN_H */

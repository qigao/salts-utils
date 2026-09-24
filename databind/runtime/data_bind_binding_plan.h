#ifndef DATA_BIND_BINDING_PLAN_H
#define DATA_BIND_BINDING_PLAN_H

#include "data_bind.h"
#include "data_bind_native.h"

#include <cmeta/function.h>
#include <cserde/reader.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_BINDING_PLAN_ABI_VERSION = 2u };

typedef enum DataBindBindingDirection {
  DATA_BIND_BINDING_INGRESS = 1,
  DATA_BIND_BINDING_EGRESS = 2
} DataBindBindingDirection;

typedef enum DataBindBindingClass {
  DATA_BIND_BINDING_VALUE = 1,
  DATA_BIND_BINDING_METADATA,
  DATA_BIND_BINDING_PAYLOAD,
  DATA_BIND_BINDING_PART,
  DATA_BIND_BINDING_RESULT,
  DATA_BIND_BINDING_ERROR
} DataBindBindingClass;

/**
 * Transport-neutral logical address.
 *
 * DataBind core interprets binding_class only. space/name/ordinal are immutable
 * projection output consumed by the selected provider adapter.
 */
typedef struct DataBindBindingAddress {
  size_t size;
  DataBindBindingClass binding_class;
  const char *space;
  const char *name;
  size_t ordinal;
} DataBindBindingAddress;

#define DATA_BIND_BINDING_ADDRESS_INIT \
  { sizeof(DataBindBindingAddress), 0, NULL, NULL, SIZE_MAX }

/**
 * Compile-time transport/protocol projection.
 *
 * The callback receives immutable DataBind reflection and emits one generic
 * address. DataBind core never switches on HTTP/RPC/MQTT/FlowMQ concepts.
 */
typedef DataBindStatus (*DataBindBindingProjectFieldFn)(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error);

typedef struct DataBindBindingProjection {
  size_t size;
  uint32_t abi_version;
  const char *id;
  void *context;
  DataBindBindingProjectFieldFn project_field;
} DataBindBindingProjection;

#define DATA_BIND_BINDING_PROJECTION_INIT \
  { sizeof(DataBindBindingProjection), DATA_BIND_BINDING_PLAN_ABI_VERSION, \
    NULL, NULL, NULL }

/** One generated DataBind state bit outside the canonical CMeta value graph. */
typedef struct DataBindNativeStateBinding {
  size_t size;
  const char *field_name;
  size_t byte_offset;
  unsigned bit;
} DataBindNativeStateBinding;

#define DATA_BIND_NATIVE_STATE_BINDING_INIT \
  { sizeof(DataBindNativeStateBinding), NULL, 0u, 0u }

/**
 * Format-neutral native representation of one DataBind IDL type.
 *
 * data is the canonical CMeta native value descriptor.
 * presence and nulls are DataBind-owned state overlays and are intentionally
 * outside the CMeta field graph.
 */
typedef struct DataBindNativeTypeBinding {
  size_t size;
  uint32_t abi_version;
  const char *idl_type_name;
  const cmeta_data_desc *data;
  const DataBindNativeStateBinding *presence;
  size_t presence_count;
  const DataBindNativeStateBinding *nulls;
  size_t null_count;
} DataBindNativeTypeBinding;

#define DATA_BIND_NATIVE_TYPE_BINDING_INIT(TYPE_NAME, DATA) \
  { sizeof(DataBindNativeTypeBinding), DATA_BIND_BINDING_PLAN_ABI_VERSION, \
    (TYPE_NAME), (DATA), NULL, 0u, NULL, 0u }

typedef DataBindStatus (*DataBindNativeDataResolverFn)(
    const cmeta_data_desc **out, DataBindError *error);

/**
 * Exact generated native layout for one Service typed-error variant.
 *
 * data_resolver resolves the canonical CMeta payload descriptor during
 * BindingPlan compilation. payload_offset is relative to the generated
 * operation error envelope.
 */
typedef struct DataBindNativeErrorBinding {
  size_t size;
  const char *idl_type_name;
  uint32_t kind_value;
  DataBindNativeDataResolverFn data_resolver;
  size_t payload_offset;
} DataBindNativeErrorBinding;

#define DATA_BIND_NATIVE_ERROR_BINDING_INIT \
  { sizeof(DataBindNativeErrorBinding), NULL, 0u, NULL, 0u }

typedef struct DataBindServiceNativeBinding {
  size_t size;
  uint32_t abi_version;
  const cmeta_function_desc *function;
  const DataBindNativeTypeBinding *request;
  const DataBindNativeTypeBinding *response;

  /** Generated typed-error envelope metadata; empty for non-throws Services. */
  const DataBindNativeErrorBinding *errors;
  size_t error_count;
  size_t error_param_index;
  size_t error_envelope_bytes;
  size_t error_kind_offset;
  size_t error_kind_bytes;
} DataBindServiceNativeBinding;

#define DATA_BIND_SERVICE_NATIVE_BINDING_INIT(FUNCTION, REQUEST, RESPONSE) \
  { sizeof(DataBindServiceNativeBinding), DATA_BIND_BINDING_PLAN_ABI_VERSION, \
    (FUNCTION), (REQUEST), (RESPONSE), NULL, 0u, SIZE_MAX, 0u, 0u, 0u }

typedef struct DataBindBindingPlanEntry {
  size_t size;
  DataBindBindingDirection direction;
  DataBindBindingAddress address;

  const char *schema_field;
  const char *function_param;
  size_t function_param_index;

  const cmeta_data_desc *data;
  size_t native_offset;
  int parameter_indirect;
  int target_is_return;

  int required;
  int has_default;
  const char *default_value;
  const char *format;

  int nullable;
  int has_presence;
  size_t presence_offset;
  unsigned presence_bit;
  int has_null;
  size_t null_offset;
  unsigned null_bit;
} DataBindBindingPlanEntry;

#define DATA_BIND_BINDING_PLAN_ENTRY_INIT \
  { sizeof(DataBindBindingPlanEntry), 0, DATA_BIND_BINDING_ADDRESS_INIT, \
    NULL, NULL, SIZE_MAX, NULL, 0u, 0, 0, 0, 0, NULL, NULL, \
    0, 0, 0u, 0u, 0, 0u, 0u }

typedef struct DataBindBindingPlanDiagnostic {
  size_t size;
  DataBindStatus status;
  char schema_field[96];
  char function_param[96];
  char message[256];
} DataBindBindingPlanDiagnostic;

#define DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT \
  { sizeof(DataBindBindingPlanDiagnostic), DATA_BIND_OK, {0}, {0}, {0} }

typedef struct DataBindBindingCallFrame {
  size_t size;

  /** Complete request root storage when the native function accepts one. */
  void *request;
  size_t request_bytes;

  /** Exact-ABI adapter owned return storage for a by-value response. */
  void *return_value;
  size_t return_bytes;

  /**
   * Semantic parameter storage indexed by cmeta_function_desc parameter index.
   * Root request storage may be aliased here by an exact-ABI adapter; the
   * BindingPlan itself uses request for root request field addressing.
   */
  void *const *params;
  const size_t *param_bytes;
  size_t param_count;
} DataBindBindingCallFrame;

#define DATA_BIND_BINDING_CALL_FRAME_INIT \
  { sizeof(DataBindBindingCallFrame), NULL, 0u, NULL, 0u, NULL, NULL, 0u }

typedef struct DataBindBindingPlan DataBindBindingPlan;

typedef enum DataBindBindingOutcomeKind {
  DATA_BIND_BINDING_OUTCOME_NONE = 0,
  DATA_BIND_BINDING_OUTCOME_SUCCESS = 1,
  DATA_BIND_BINDING_OUTCOME_NATIVE_STATUS = 2,
  DATA_BIND_BINDING_OUTCOME_TYPED_ERROR = 3
} DataBindBindingOutcomeKind;

/**
 * Semantic result of one exact Service invocation after bridge/admission
 * succeeded. typed_error is borrowed from the immutable BindingPlan.
 */
typedef struct DataBindBindingOutcome {
  size_t size;
  DataBindBindingOutcomeKind kind;
  int native_status;
  size_t typed_error_index;
  const char *typed_error;
} DataBindBindingOutcome;

#define DATA_BIND_BINDING_OUTCOME_INIT \
  { sizeof(DataBindBindingOutcome), DATA_BIND_BINDING_OUTCOME_NONE, 0, \
    SIZE_MAX, NULL }

/**
 * Runtime logical provider. It receives only precompiled generic addresses.
 * It never walks DataBind schema AST or owns transport/session state.
 */
typedef enum DataBindBindingValueState {
  DATA_BIND_VALUE_STATE_ABSENT = 0,
  DATA_BIND_VALUE_STATE_VALUE = 1,
  DATA_BIND_VALUE_STATE_NULL = 2
} DataBindBindingValueState;

typedef DataBindStatus (*DataBindBindingOpenInputFn)(
    void *context, const DataBindBindingPlanEntry *entry,
    cserde_reader *reader, DataBindBindingValueState *state,
    DataBindError *error);
typedef DataBindStatus (*DataBindBindingBeginOutputFn)(
    void *context, DataBindError *error);
typedef DataBindStatus (*DataBindBindingWriteOutputFn)(
    void *context, const DataBindBindingPlanEntry *entry,
    DataBindBindingValueState state, const void *value, size_t value_bytes,
    DataBindError *error);
typedef DataBindStatus (*DataBindBindingCommitOutputFn)(
    void *context, DataBindError *error);
typedef void (*DataBindBindingAbortOutputFn)(void *context);

typedef struct DataBindBindingProvider {
  size_t size;
  uint32_t abi_version;
  void *context;
  DataBindBindingOpenInputFn open_input;
  DataBindBindingBeginOutputFn begin_output;
  DataBindBindingWriteOutputFn write_output;
  DataBindBindingCommitOutputFn commit_output;
  DataBindBindingAbortOutputFn abort_output;
} DataBindBindingProvider;

#define DATA_BIND_BINDING_PROVIDER_INIT \
  { sizeof(DataBindBindingProvider), DATA_BIND_BINDING_PLAN_ABI_VERSION, \
    NULL, NULL, NULL, NULL, NULL, NULL }


/**
 * Compile one Service operation into an immutable generic BindingPlan.
 *
 * The compiler joins DataBind logical contract semantics, a compile-time
 * projection adapter and CMeta native function/data semantics. No schema AST
 * node or codec-owned string is retained in the plan.
 */
DATA_BIND_API DataBindStatus data_bind_binding_plan_compile_service(
    DataBind *codec,
    const char *service_name,
    const char *operation_name,
    const DataBindBindingProjection *projection,
    const DataBindServiceNativeBinding *native,
    DataBindBindingPlan **out_plan,
    DataBindBindingPlanDiagnostic *diagnostic);

DATA_BIND_API void data_bind_binding_plan_free(DataBindBindingPlan *plan);

DATA_BIND_API const char *
data_bind_binding_plan_operation_id(const DataBindBindingPlan *plan);
DATA_BIND_API const char *
data_bind_binding_plan_projection_id(const DataBindBindingPlan *plan);
DATA_BIND_API const cmeta_function_desc *
data_bind_binding_plan_function(const DataBindBindingPlan *plan);

DATA_BIND_API size_t
data_bind_binding_plan_ingress_count(const DataBindBindingPlan *plan);
DATA_BIND_API size_t
data_bind_binding_plan_egress_count(const DataBindBindingPlan *plan);

DATA_BIND_API int data_bind_binding_plan_ingress_at(
    const DataBindBindingPlan *plan, size_t index, DataBindBindingPlanEntry *out);
DATA_BIND_API int data_bind_binding_plan_egress_at(
    const DataBindBindingPlan *plan, size_t index, DataBindBindingPlanEntry *out);

DATA_BIND_API size_t
data_bind_binding_plan_error_count(const DataBindBindingPlan *plan);
DATA_BIND_API const char *
data_bind_binding_plan_error_at(const DataBindBindingPlan *plan, size_t index);

/**
 * Decode provider inputs into caller-owned native staging.
 *
 * Runtime executes only immutable plan entries. On failure, every request /
 * parameter staging location initialized by this call is restored to canonical
 * semantic zero, including DataBind optional-presence bits. Return-value
 * storage is not modified by ingress binding.
 */
DATA_BIND_API DataBindStatus data_bind_binding_plan_bind_inputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindNativeOptions *native_options,
    DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic);

/**
 * Publish response values transactionally. begin/write/commit failures do not
 * report success and any write/commit failure calls abort_output.
 */
DATA_BIND_API DataBindStatus data_bind_binding_plan_write_outputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic);

/**
 * Publish exactly one completed Service outcome.
 *
 * bridge/admission failure is deliberately outside this API. After an exact
 * invocation succeeds:
 * - native_status != 0 publishes no response/error payload;
 * - native_status == 0 and typed-error kind == NONE publishes success response;
 * - native_status == 0 and a valid typed-error kind transactionally publishes
 *   exactly one DATA_BIND_BINDING_ERROR payload.
 *
 * Ambiguous native-status + typed-error combinations are rejected.
 */
DATA_BIND_API DataBindStatus data_bind_binding_plan_write_outcome(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindBindingCallFrame *frame,
    int native_status,
    DataBindBindingOutcome *outcome,
    DataBindBindingPlanDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_BINDING_PLAN_H */

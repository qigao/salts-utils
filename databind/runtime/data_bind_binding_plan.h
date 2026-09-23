#ifndef DATA_BIND_BINDING_PLAN_H
#define DATA_BIND_BINDING_PLAN_H

#include "data_bind.h"
#include "data_bind_native.h"

#include <cmeta/function.h>
#include <cserde/reader.h>

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(DATA_BIND_BUILD_DLL)
#define DATA_BIND_BINDING_API __declspec(dllexport)
#else
#define DATA_BIND_BINDING_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_BINDING_PLAN_ABI_VERSION = 1u };

typedef enum DataBindBindingDirection {
  DATA_BIND_BINDING_INGRESS = 1,
  DATA_BIND_BINDING_EGRESS = 2
} DataBindBindingDirection;

/**
 * Transport-neutral logical provider vocabulary.
 *
 * Projection adapters translate transport-specific concepts into these
 * classes at compile/control time. The hot-path provider therefore never
 * needs to know DataBind schema annotations or another transport's types.
 */
typedef enum DataBindBindingClass {
  DATA_BIND_BINDING_VALUE = 1,
  DATA_BIND_BINDING_METADATA,
  DATA_BIND_BINDING_PAYLOAD,
  DATA_BIND_BINDING_PART,
  DATA_BIND_BINDING_RESULT,
  DATA_BIND_BINDING_ERROR
} DataBindBindingClass;

typedef enum DataBindBindingNativeTarget {
  DATA_BIND_BINDING_TARGET_REQUEST_FIELD = 1,
  DATA_BIND_BINDING_TARGET_FUNCTION_PARAM,
  DATA_BIND_BINDING_TARGET_RETURN_FIELD
} DataBindBindingNativeTarget;

/**
 * Compile-time projection result for one logical schema field.
 *
 * selector is projection-owned only for the duration of the callback.
 * The compiled plan copies it. Examples:
 *   HTTP adapter:   class=VALUE,    selector="path:id"
 *   HTTP adapter:   class=METADATA, selector="header:Authorization"
 *   MQTT adapter:   class=VALUE,    selector="topic:device_id"
 *   FlowMQ adapter: class=PART,     selector="part:1"
 *
 * DataBind core never interprets selector.
 */
typedef struct DataBindBindingProjectionSlot {
  size_t size;
  DataBindBindingClass binding_class;
  const char *selector;
} DataBindBindingProjectionSlot;

#define DATA_BIND_BINDING_PROJECTION_SLOT_INIT \
  { sizeof(DataBindBindingProjectionSlot), (DataBindBindingClass)0, NULL }

typedef DataBindStatus (*DataBindBindingProjectFieldFn)(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingProjectionSlot *out, DataBindError *error);

/**
 * Projection adapter used only while compiling a plan.
 *
 * id is copied into the plan for diagnostics/identity. project_field receives
 * immutable service/field reflection, never schema AST nodes.
 */
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

/** Optional-presence mapping owned by DataBind native binding metadata. */
typedef struct DataBindNativePresenceBinding {
  size_t size;
  const char *field_name;
  size_t byte_offset;
  unsigned bit_index;
} DataBindNativePresenceBinding;

#define DATA_BIND_NATIVE_PRESENCE_BINDING_INIT \
  { sizeof(DataBindNativePresenceBinding), NULL, 0u, 0u }

/**
 * Format-neutral native record binding.
 *
 * data is the canonical CMeta native Struct descriptor. Optional-presence
 * metadata belongs to DataBind's logical contract and is intentionally
 * separate from CMeta storage/type semantics.
 */
typedef struct DataBindNativeRecordBinding {
  size_t size;
  uint32_t abi_version;
  const char *schema_type;
  const cmeta_data_desc *data;
  const DataBindNativePresenceBinding *presence;
  size_t presence_count;
} DataBindNativeRecordBinding;

#define DATA_BIND_NATIVE_RECORD_BINDING_INIT(SCHEMA_TYPE, DATA) \
  { sizeof(DataBindNativeRecordBinding), DATA_BIND_BINDING_PLAN_ABI_VERSION, \
    (SCHEMA_TYPE), (DATA), NULL, 0u }

/**
 * Native implementation binding for one service operation.
 *
 * Function semantics come exclusively from CMeta. request/response describe
 * native host record storage without introducing a second type system.
 */
typedef struct DataBindServiceNativeBinding {
  size_t size;
  uint32_t abi_version;
  const cmeta_function_desc *function;
  const DataBindNativeRecordBinding *request;
  const DataBindNativeRecordBinding *response;
} DataBindServiceNativeBinding;

#define DATA_BIND_SERVICE_NATIVE_BINDING_INIT(FUNCTION, REQUEST, RESPONSE) \
  { sizeof(DataBindServiceNativeBinding), DATA_BIND_BINDING_PLAN_ABI_VERSION, \
    (FUNCTION), (REQUEST), (RESPONSE) }

typedef struct DataBindBindingPlanEntry {
  size_t size;
  DataBindBindingDirection direction;
  DataBindBindingClass binding_class;
  DataBindBindingNativeTarget target;

  /** Plan-owned logical/schema identity. */
  const char *schema_field;
  const char *logical_name;

  /** Plan-owned projection-specific opaque selector. */
  const char *selector;

  /** Plan-owned function parameter name when a parameter participates. */
  const char *function_param;
  size_t function_param_index;

  /**
   * Byte offset within the selected root target. Direct scalar parameters use
   * zero; root request/response projections use their CMeta field offset.
   */
  size_t native_offset;
  int parameter_indirect;

  int required;
  int has_default;
  const char *default_value;
  const char *format;
  const cmeta_data_desc *data;

  int has_presence;
  size_t presence_offset;
  unsigned presence_bit;
} DataBindBindingPlanEntry;

#define DATA_BIND_BINDING_PLAN_ENTRY_INIT \
  { sizeof(DataBindBindingPlanEntry) }

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
  void *request;
  size_t request_bytes;
  void *return_value;
  size_t return_bytes;
  void *const *params;
  const size_t *param_bytes;
  size_t param_count;
} DataBindBindingCallFrame;

#define DATA_BIND_BINDING_CALL_FRAME_INIT \
  { sizeof(DataBindBindingCallFrame), NULL, 0u, NULL, 0u, NULL, NULL, 0u }

typedef struct DataBindBindingPlan DataBindBindingPlan;

/**
 * Runtime provider input callback. The provider interprets only binding_class
 * and selector. It never receives transport-specific enums from DataBind core.
 */
typedef DataBindStatus (*DataBindBindingOpenInputFn)(
    void *context, const DataBindBindingPlanEntry *entry,
    cserde_reader *reader, int *present, DataBindError *error);

typedef DataBindStatus (*DataBindBindingBeginOutputFn)(
    void *context, DataBindError *error);
typedef DataBindStatus (*DataBindBindingWriteOutputFn)(
    void *context, const DataBindBindingPlanEntry *entry,
    const void *value, size_t value_bytes, DataBindError *error);
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
 * Compile one service operation into an immutable generic binding plan.
 *
 * The compiler joins:
 *   DataBind logical contract
 * + projection adapter
 * + CMeta native function/type semantics.
 *
 * No schema AST node or codec-owned string is retained. The projection adapter
 * is compile-time only. CMeta/native descriptors are borrowed immutable
 * metadata and must outlive the plan.
 */
DATA_BIND_BINDING_API DataBindStatus data_bind_binding_plan_compile_service(
    DataBind *codec, const char *service_name, const char *operation_name,
    const DataBindBindingProjection *projection,
    const DataBindServiceNativeBinding *native,
    DataBindBindingPlan **out_plan,
    DataBindBindingPlanDiagnostic *diagnostic);

DATA_BIND_BINDING_API void data_bind_binding_plan_free(
    DataBindBindingPlan *plan);

DATA_BIND_BINDING_API const char *data_bind_binding_plan_operation_id(
    const DataBindBindingPlan *plan);
DATA_BIND_BINDING_API const char *data_bind_binding_plan_projection_id(
    const DataBindBindingPlan *plan);
DATA_BIND_BINDING_API const cmeta_function_desc *
data_bind_binding_plan_function(const DataBindBindingPlan *plan);

DATA_BIND_BINDING_API size_t data_bind_binding_plan_ingress_count(
    const DataBindBindingPlan *plan);
DATA_BIND_BINDING_API size_t data_bind_binding_plan_egress_count(
    const DataBindBindingPlan *plan);
DATA_BIND_BINDING_API int data_bind_binding_plan_ingress_at(
    const DataBindBindingPlan *plan, size_t index,
    DataBindBindingPlanEntry *out);
DATA_BIND_BINDING_API int data_bind_binding_plan_egress_at(
    const DataBindBindingPlan *plan, size_t index,
    DataBindBindingPlanEntry *out);

DATA_BIND_BINDING_API size_t data_bind_binding_plan_error_count(
    const DataBindBindingPlan *plan);
DATA_BIND_BINDING_API const char *data_bind_binding_plan_error_at(
    const DataBindBindingPlan *plan, size_t index);

/**
 * Initialize/bind ingress staging. Runtime executes only precompiled entries;
 * it never re-walks DataBind schema/service reflection.
 *
 * On failure every request/parameter staging location initialized by this call
 * is restored to canonical semantic zero, including DataBind presence bits.
 * Return-value storage is never touched by ingress binding.
 */
DATA_BIND_BINDING_API DataBindStatus data_bind_binding_plan_bind_inputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindNativeOptions *native_options,
    DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic);

/**
 * Publish egress values transactionally. Optional root response fields whose
 * presence bit is clear are omitted. Any write/commit failure aborts provider
 * staging.
 */
DATA_BIND_BINDING_API DataBindStatus data_bind_binding_plan_write_outputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_BINDING_PLAN_H */

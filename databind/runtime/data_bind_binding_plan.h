#ifndef DATA_BIND_BINDING_PLAN_H
#define DATA_BIND_BINDING_PLAN_H

#include "data_bind.h"

#include <cmeta/function.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_BINDING_PLAN_ABI_VERSION = 1u };

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
 * DataBind core interprets class only. space/name are immutable compiler
 * output consumed by the selected provider adapter, e.g.:
 *
 *   VALUE    + "http.query" + "limit"
 *   METADATA + "http.header" + "Authorization"
 *   VALUE    + "rpc.param" + "id"
 *   PAYLOAD  + "mqtt.payload" + NULL
 *
 * Adding another transport does not extend a closed protocol enum.
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

/** Optional-presence layout generated for one native DataBind value type. */
typedef struct DataBindNativePresenceBinding {
  size_t size;
  const char *field_name;
  size_t byte_offset;
  unsigned bit;
} DataBindNativePresenceBinding;

#define DATA_BIND_NATIVE_PRESENCE_BINDING_INIT \
  { sizeof(DataBindNativePresenceBinding), NULL, 0u, 0u }

/**
 * Format-neutral native representation of one DataBind IDL type.
 *
 * data is the canonical CMeta native data descriptor. Field names/data/offsets
 * come from its CMETA_DATA_STRUCT shape. presence only describes DataBind
 * optional-presence storage that is not a CMeta type semantic.
 */
typedef struct DataBindNativeTypeBinding {
  size_t size;
  uint32_t abi_version;
  const char *idl_type_name;
  const cmeta_data_desc *data;
  const DataBindNativePresenceBinding *presence;
  size_t presence_count;
} DataBindNativeTypeBinding;

#define DATA_BIND_NATIVE_TYPE_BINDING_INIT(TYPE_NAME, DATA) \
  { sizeof(DataBindNativeTypeBinding), DATA_BIND_BINDING_PLAN_ABI_VERSION, \
    (TYPE_NAME), (DATA), NULL, 0u }

typedef struct DataBindServiceNativeBinding {
  size_t size;
  uint32_t abi_version;
  const cmeta_function_desc *function;
  const DataBindNativeTypeBinding *request;
  const DataBindNativeTypeBinding *response;
} DataBindServiceNativeBinding;

#define DATA_BIND_SERVICE_NATIVE_BINDING_INIT(FUNCTION, REQUEST, RESPONSE) \
  { sizeof(DataBindServiceNativeBinding), DATA_BIND_BINDING_PLAN_ABI_VERSION, \
    (FUNCTION), (REQUEST), (RESPONSE) }

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

  int has_presence;
  size_t presence_offset;
  unsigned presence_bit;
} DataBindBindingPlanEntry;

#define DATA_BIND_BINDING_PLAN_ENTRY_INIT \
  { sizeof(DataBindBindingPlanEntry), 0, DATA_BIND_BINDING_ADDRESS_INIT, \
    NULL, NULL, SIZE_MAX, NULL, 0u, 0, 0, 0, 0, NULL, NULL, 0, 0u, 0u }

typedef struct DataBindBindingPlanDiagnostic {
  size_t size;
  DataBindStatus status;
  char schema_field[96];
  char function_param[96];
  char message[256];
} DataBindBindingPlanDiagnostic;

#define DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT \
  { sizeof(DataBindBindingPlanDiagnostic), DATA_BIND_OK, {0}, {0}, {0} }

typedef struct DataBindBindingPlan DataBindBindingPlan;

/**
 * Compile one Service operation into an immutable generic BindingPlan.
 *
 * projection_id selects an IDL transport projection ("http", "rpc", later
 * messaging providers) without becoming a closed public transport enum.
 *
 * The compiler joins DataBind logical/wire semantics with CMeta native
 * function/data semantics. It never creates an invocation ABI and never
 * requires TBE wire descriptors.
 */
DATA_BIND_API DataBindStatus data_bind_service_binding_plan_compile(
    DataBind *codec,
    const char *service_name,
    const char *operation_name,
    const char *projection_id,
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

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_BINDING_PLAN_H */

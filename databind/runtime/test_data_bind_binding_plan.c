#include "data_bind_binding_plan.h"
#include "data_bind_message_plan.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <cflow/executor.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct AddRequest {
  uint32_t left;
  uint32_t right;
  uint32_t scale;
  uint8_t presence;
} AddRequest;

typedef struct AddResponse {
  uint32_t sum;
} AddResponse;

static const cmeta_type_identity ADD_REQUEST_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.calc.AddRequest");
static const cmeta_type_identity ADD_RESPONSE_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.calc.AddResponse");

static const cmeta_type_traits ADD_RESPONSE_TRAITS = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};

static const cmeta_type_desc ADD_REQUEST_TYPE = {
    "AddRequest", sizeof(AddRequest), _Alignof(AddRequest),
    CMETA_T_OBJECT, NULL, NULL, &ADD_REQUEST_ID};
static const cmeta_type_desc ADD_RESPONSE_TYPE = {
    "AddResponse", sizeof(AddResponse), _Alignof(AddResponse),
    CMETA_T_OBJECT, NULL, &ADD_RESPONSE_TRAITS, &ADD_RESPONSE_ID};

static const cmeta_type_desc ADD_REQUEST_PTR_TYPE = {
    "const AddRequest *", sizeof(AddRequest *), _Alignof(AddRequest *),
    CMETA_T_POINTER, &ADD_REQUEST_TYPE, NULL, NULL};
static const cmeta_type_desc ADD_REQUEST_RECEIVER_PTR_TYPE = {
    "AddRequest *", sizeof(AddRequest *), _Alignof(AddRequest *),
    CMETA_T_POINTER, &ADD_REQUEST_TYPE, NULL, NULL};
static const cmeta_type_desc ADD_RESPONSE_PTR_TYPE = {
    "AddResponse *", sizeof(AddResponse *), _Alignof(AddResponse *),
    CMETA_T_POINTER, &ADD_RESPONSE_TYPE, NULL, NULL};
static const cmeta_type_desc UINT32_PTR_TYPE = {
    "uint32_t *", sizeof(uint32_t *), _Alignof(uint32_t *),
    CMETA_T_POINTER, &cmeta_type_uint32, NULL, NULL};

static const cmeta_field_desc ADD_REQUEST_LAYOUT_FIELDS[] = {
    {"left", "uint32_t", offsetof(AddRequest, left), sizeof(uint32_t),
     _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"right", "uint32_t", offsetof(AddRequest, right), sizeof(uint32_t),
     _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"scale", "uint32_t", offsetof(AddRequest, scale), sizeof(uint32_t),
     _Alignof(uint32_t), &cmeta_type_uint32, NULL}};
static const cmeta_struct_desc ADD_REQUEST_LAYOUT = {
    "AddRequest", sizeof(AddRequest), _Alignof(AddRequest),
    ADD_REQUEST_LAYOUT_FIELDS, 3u};
static const cmeta_data_field_desc ADD_REQUEST_FIELDS[] = {
    {"test.calc.AddRequest.left", "left", offsetof(AddRequest, left),
     &cmeta_data_uint32},
    {"test.calc.AddRequest.right", "right", offsetof(AddRequest, right),
     &cmeta_data_uint32},
    {"test.calc.AddRequest.scale", "scale", offsetof(AddRequest, scale),
     &cmeta_data_uint32}};
static const cmeta_data_struct_shape ADD_REQUEST_SHAPE = {
    &ADD_REQUEST_LAYOUT, ADD_REQUEST_FIELDS, 3u};
static const cmeta_data_desc ADD_REQUEST_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.calc.AddRequest.data",
    .display_name = "AddRequest",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &ADD_REQUEST_TYPE,
    .shape = &ADD_REQUEST_SHAPE};

static const cmeta_field_desc ADD_RESPONSE_LAYOUT_FIELDS[] = {
    {"sum", "uint32_t", offsetof(AddResponse, sum), sizeof(uint32_t),
     _Alignof(uint32_t), &cmeta_type_uint32, NULL}};
static const cmeta_struct_desc ADD_RESPONSE_LAYOUT = {
    "AddResponse", sizeof(AddResponse), _Alignof(AddResponse),
    ADD_RESPONSE_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc ADD_RESPONSE_FIELDS[] = {
    {"test.calc.AddResponse.sum", "sum", offsetof(AddResponse, sum),
     &cmeta_data_uint32}};
static const cmeta_data_struct_shape ADD_RESPONSE_SHAPE = {
    &ADD_RESPONSE_LAYOUT, ADD_RESPONSE_FIELDS, 1u};
static const cmeta_data_desc ADD_RESPONSE_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.calc.AddResponse.data",
    .display_name = "AddResponse",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &ADD_RESPONSE_TYPE,
    .shape = &ADD_RESPONSE_SHAPE};

static const DataBindNativeStateBinding ADD_REQUEST_PRESENCE[] = {
    {sizeof(DataBindNativeStateBinding), "scale",
     offsetof(AddRequest, presence), 0u}};

static const DataBindNativeTypeBinding ADD_REQUEST_NATIVE = {
    sizeof(DataBindNativeTypeBinding),
    DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "AddRequest",
    &ADD_REQUEST_DATA,
    ADD_REQUEST_PRESENCE,
    1u,
    NULL,
    0u};

static const DataBindNativeTypeBinding ADD_RESPONSE_NATIVE = {
    sizeof(DataBindNativeTypeBinding),
    DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "AddResponse",
    &ADD_RESPONSE_DATA,
    NULL,
    0u,
    NULL,
    0u};

FunctionDeclAs(
    value, int, &cmeta_type_int, calc_add_root,
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE),
    (AddResponse *, response, CMETA_PARAM_OUT, &ADD_RESPONSE_PTR_TYPE));

FunctionDeclAs(
    value, int, &cmeta_type_int, calc_add_receiver_root,
    (AddRequest *, self,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_RECEIVER,
     &ADD_REQUEST_RECEIVER_PTR_TYPE),
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE),
    (AddResponse *, response, CMETA_PARAM_OUT, &ADD_RESPONSE_PTR_TYPE));

FunctionDeclAs(
    value, int, &cmeta_type_int, calc_add_fields,
    (uint32_t, left, CMETA_PARAM_IN, &cmeta_type_uint32),
    (uint32_t, right, CMETA_PARAM_IN, &cmeta_type_uint32),
    (uint32_t, scale, CMETA_PARAM_IN, &cmeta_type_uint32),
    (uint32_t *, sum, CMETA_PARAM_OUT, &UINT32_PTR_TYPE));

FunctionDeclAs(
    value, AddResponse *, &ADD_RESPONSE_PTR_TYPE, calc_bad_pointer_return,
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE));

FunctionDeclAs(
    value, AddResponse, &ADD_RESPONSE_TYPE, calc_value_return,
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE));

typedef struct DeferredCallProbe {
  DataBindBindingCallLifetime *lifetime;
  AddResponse *response;
  unsigned run_calls;
  unsigned cancel_calls;
  unsigned finalize_calls;
  int native_status;
  DataBindStatus restore_status;
} DeferredCallProbe;

static void deferred_call_run(void *context) {
  DeferredCallProbe *probe = (DeferredCallProbe *)context;
  ++probe->run_calls;
  probe->response->sum = 7u;
  probe->native_status = -1;
}

static void deferred_call_cancel(void *context) {
  DeferredCallProbe *probe = (DeferredCallProbe *)context;
  ++probe->cancel_calls;
}

static void deferred_call_finalize(void *context) {
  DeferredCallProbe *probe = (DeferredCallProbe *)context;
  DataBindBindingPlanDiagnostic diagnostic = DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
  ++probe->finalize_calls;
  probe->restore_status = data_bind_binding_call_restore_zero(probe->lifetime, &diagnostic);
}

static DataBind *create_codec(void) {
  static const char schema[] =
      "message AddRequest {"
      " @Min(1) @Max(5) uint32 left;"
      " @Min(1) @Max(10) uint32 right;"
      " optional @Min(1) @Max(3) uint32 scale default 1;"
      "}"
      "message AddResponse { @Min(1) @Max(100) uint32 sum; }"
      "service Calc {"
      " Add: AddRequest -> AddResponse;"
      "}"
      "channel AddInput: AddRequest;";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;

  check_equal(data_bind_create_from_text(
                  schema, sizeof(schema) - 1u, &codec, &error),
              DATA_BIND_OK);
  return codec;
}

static DataBindServiceNativeBinding native_binding(
    const cmeta_function_desc *function) {
  return (DataBindServiceNativeBinding)
      DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
          function, &ADD_REQUEST_NATIVE, &ADD_RESPONSE_NATIVE);
}


typedef struct RollbackProbeBuffer {
  size_t size;
  unsigned char bytes[8];
} RollbackProbeBuffer;

typedef struct RollbackRequest {
  RollbackProbeBuffer first;
  RollbackProbeBuffer second;
  RollbackProbeBuffer third;
} RollbackRequest;

typedef struct RollbackLifecycleProbe {
  size_t init_calls[3];
  size_t restore_calls[3];
  size_t restore_order_count;
  unsigned restore_order[4];
} RollbackLifecycleProbe;

static RollbackLifecycleProbe ROLLBACK_LIFECYCLE_PROBE;

static void rollback_probe_reset(void) {
  memset(&ROLLBACK_LIFECYCLE_PROBE, 0, sizeof(ROLLBACK_LIFECYCLE_PROBE));
}

static bool rollback_buffer_is_zero(const void *object) {
  const RollbackProbeBuffer *buffer =
      (const RollbackProbeBuffer *)object;
  return buffer != NULL && buffer->size == 0u;
}

static cmeta_status rollback_buffer_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
  RollbackProbeBuffer *buffer = (RollbackProbeBuffer *)object;
  if (buffer == NULL || (size != 0u && data == NULL))
    return CMETA_INVALID_ARGUMENT;
  if (size > sizeof(buffer->bytes) || size > max_bytes)
    return CMETA_CAPACITY_EXCEEDED;
  memset(buffer, 0, sizeof(*buffer));
  if (size != 0u) memcpy(buffer->bytes, data, size);
  buffer->size = size;
  return CMETA_OK;
}

static cmeta_status rollback_buffer_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
  const RollbackProbeBuffer *buffer =
      (const RollbackProbeBuffer *)object;
  if (buffer == NULL || out_data == NULL || out_size == NULL)
    return CMETA_INVALID_ARGUMENT;
  *out_data = buffer->bytes;
  *out_size = buffer->size;
  return CMETA_OK;
}

static void rollback_buffer_move(void *destination, void *source) {
  RollbackProbeBuffer *to = (RollbackProbeBuffer *)destination;
  RollbackProbeBuffer *from = (RollbackProbeBuffer *)source;
  if (to == NULL || from == NULL) return;
  *to = *from;
  memset(from, 0, sizeof(*from));
}

static cmeta_status rollback_buffer_init_common(
    void *object, unsigned slot, int fail) {
  RollbackProbeBuffer *buffer = (RollbackProbeBuffer *)object;
  if (buffer == NULL || slot >= 3u) return CMETA_INVALID_ARGUMENT;
  ++ROLLBACK_LIFECYCLE_PROBE.init_calls[slot];
  memset(buffer, 0, sizeof(*buffer));
  return fail ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void rollback_buffer_restore_common(void *object, unsigned slot) {
  RollbackProbeBuffer *buffer = (RollbackProbeBuffer *)object;
  if (buffer == NULL || slot >= 3u) return;
  ++ROLLBACK_LIFECYCLE_PROBE.restore_calls[slot];
  /*
   * Only successfully initialized parameters participate in the BindingPlan
   * rollback order proof. The failing third provider may restore internally
   * while making its own failed init failure-atomic.
   */
  if (slot < 2u &&
      ROLLBACK_LIFECYCLE_PROBE.restore_order_count <
          sizeof(ROLLBACK_LIFECYCLE_PROBE.restore_order) /
              sizeof(ROLLBACK_LIFECYCLE_PROBE.restore_order[0])) {
    ROLLBACK_LIFECYCLE_PROBE.restore_order[
        ROLLBACK_LIFECYCLE_PROBE.restore_order_count++] = slot;
  }
  memset(buffer, 0, sizeof(*buffer));
}

static cmeta_status rollback_buffer_init_first(void *object) {
  return rollback_buffer_init_common(object, 0u, 0);
}

static cmeta_status rollback_buffer_init_second(void *object) {
  return rollback_buffer_init_common(object, 1u, 0);
}

static cmeta_status rollback_buffer_init_third(void *object) {
  return rollback_buffer_init_common(object, 2u, 1);
}

static void rollback_buffer_restore_first(void *object) {
  rollback_buffer_restore_common(object, 0u);
}

static void rollback_buffer_restore_second(void *object) {
  rollback_buffer_restore_common(object, 1u);
}

static void rollback_buffer_restore_third(void *object) {
  rollback_buffer_restore_common(object, 2u);
}

static const cmeta_type_identity ROLLBACK_BUFFER_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.binding.RollbackProbeBuffer");

static const cmeta_type_desc ROLLBACK_BUFFER_TYPE = {
    "RollbackProbeBuffer", sizeof(RollbackProbeBuffer),
    _Alignof(RollbackProbeBuffer), CMETA_T_OBJECT, NULL, NULL,
    &ROLLBACK_BUFFER_ID};

static const cmeta_data_buffer_shape ROLLBACK_BUFFER_SHAPE = {
    CMETA_DATA_BUFFER_OWNED};

#define ROLLBACK_BUFFER_OPS(NAME, INIT, RESTORE)                              \
  static const cmeta_data_buffer_ops NAME = {                                 \
      sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,       \
      &ROLLBACK_BUFFER_TYPE, CMETA_DATA_BUFFER_OWNED,                         \
      rollback_buffer_is_zero, rollback_buffer_assign, RESTORE,               \
      rollback_buffer_read, INIT, rollback_buffer_move}

ROLLBACK_BUFFER_OPS(
    ROLLBACK_FIRST_OPS, rollback_buffer_init_first,
    rollback_buffer_restore_first);
ROLLBACK_BUFFER_OPS(
    ROLLBACK_SECOND_OPS, rollback_buffer_init_second,
    rollback_buffer_restore_second);
ROLLBACK_BUFFER_OPS(
    ROLLBACK_THIRD_OPS, rollback_buffer_init_third,
    rollback_buffer_restore_third);

#undef ROLLBACK_BUFFER_OPS

#define ROLLBACK_BUFFER_DATA(NAME, STABLE, DISPLAY, OPS)                      \
  static const cmeta_data_desc NAME = {                                       \
      .struct_size = sizeof(cmeta_data_desc),                                 \
      .abi_version = CMETA_DATA_DESC_ABI_VERSION,                             \
      .stable_id = STABLE,                                                    \
      .display_name = DISPLAY,                                                \
      .kind = CMETA_DATA_BYTES,                                               \
      .storage_type = &ROLLBACK_BUFFER_TYPE,                                  \
      .shape = &ROLLBACK_BUFFER_SHAPE,                                        \
      .buffer_ops = &(OPS)}

ROLLBACK_BUFFER_DATA(
    ROLLBACK_FIRST_DATA, "test.binding.rollback.first",
    "RollbackFirst", ROLLBACK_FIRST_OPS);
ROLLBACK_BUFFER_DATA(
    ROLLBACK_SECOND_DATA, "test.binding.rollback.second",
    "RollbackSecond", ROLLBACK_SECOND_OPS);
ROLLBACK_BUFFER_DATA(
    ROLLBACK_THIRD_DATA, "test.binding.rollback.third",
    "RollbackThird", ROLLBACK_THIRD_OPS);

#undef ROLLBACK_BUFFER_DATA

static const cmeta_type_identity ROLLBACK_REQUEST_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.binding.RollbackRequest");

static const cmeta_type_desc ROLLBACK_REQUEST_TYPE = {
    "RollbackRequest", sizeof(RollbackRequest), _Alignof(RollbackRequest),
    CMETA_T_OBJECT, NULL, NULL, &ROLLBACK_REQUEST_ID};

static const cmeta_field_desc ROLLBACK_REQUEST_LAYOUT_FIELDS[] = {
    {"first", "bytes", offsetof(RollbackRequest, first),
     sizeof(RollbackProbeBuffer), _Alignof(RollbackProbeBuffer),
     &ROLLBACK_BUFFER_TYPE, NULL},
    {"second", "bytes", offsetof(RollbackRequest, second),
     sizeof(RollbackProbeBuffer), _Alignof(RollbackProbeBuffer),
     &ROLLBACK_BUFFER_TYPE, NULL},
    {"third", "bytes", offsetof(RollbackRequest, third),
     sizeof(RollbackProbeBuffer), _Alignof(RollbackProbeBuffer),
     &ROLLBACK_BUFFER_TYPE, NULL}};

static const cmeta_struct_desc ROLLBACK_REQUEST_LAYOUT = {
    "RollbackRequest", sizeof(RollbackRequest), _Alignof(RollbackRequest),
    ROLLBACK_REQUEST_LAYOUT_FIELDS, 3u};

static const cmeta_data_field_desc ROLLBACK_REQUEST_FIELDS[] = {
    {"test.binding.RollbackRequest.first", "first",
     offsetof(RollbackRequest, first), &ROLLBACK_FIRST_DATA},
    {"test.binding.RollbackRequest.second", "second",
     offsetof(RollbackRequest, second), &ROLLBACK_SECOND_DATA},
    {"test.binding.RollbackRequest.third", "third",
     offsetof(RollbackRequest, third), &ROLLBACK_THIRD_DATA}};

static const cmeta_data_struct_shape ROLLBACK_REQUEST_SHAPE = {
    &ROLLBACK_REQUEST_LAYOUT, ROLLBACK_REQUEST_FIELDS, 3u};

static const cmeta_data_desc ROLLBACK_REQUEST_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.binding.RollbackRequest.data",
    .display_name = "RollbackRequest",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &ROLLBACK_REQUEST_TYPE,
    .shape = &ROLLBACK_REQUEST_SHAPE};

static const DataBindNativeTypeBinding ROLLBACK_REQUEST_NATIVE = {
    sizeof(DataBindNativeTypeBinding),
    DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "RollbackRequest",
    &ROLLBACK_REQUEST_DATA,
    NULL,
    0u,
    NULL,
    0u};

FunctionDecl(
    value, void, rollback_bind_fields,
    (RollbackProbeBuffer, first, CMETA_PARAM_IN, &ROLLBACK_BUFFER_TYPE),
    (RollbackProbeBuffer, second, CMETA_PARAM_IN, &ROLLBACK_BUFFER_TYPE),
    (RollbackProbeBuffer, third, CMETA_PARAM_IN, &ROLLBACK_BUFFER_TYPE));

static DataBind *create_rollback_codec(void) {
  static const char schema[] =
      "message RollbackRequest {"
      " bytes first;"
      " bytes second;"
      " bytes third;"
      "}"
      "service Rollback {"
      " Run: RollbackRequest -> void;"
      "}";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;

  check_equal(data_bind_create_from_text(
                  schema, sizeof(schema) - 1u, &codec, &error),
              DATA_BIND_OK);
  return codec;
}

static DataBindServiceNativeBinding rollback_native_binding(void) {
  return (DataBindServiceNativeBinding)
      DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
          FunctionMeta(rollback_bind_fields),
          &ROLLBACK_REQUEST_NATIVE, NULL);
}


typedef struct StateRequest {
  uint32_t required_value;
  uint32_t optional_value;
  uint32_t nullable_value;
  uint32_t defaulted_value;
  uint8_t presence;
  uint8_t nulls;
} StateRequest;

typedef struct StateResponse {
  uint32_t nullable_result;
  uint32_t tri_result;
  uint8_t presence;
  uint8_t nulls;
} StateResponse;

static const cmeta_type_identity STATE_REQUEST_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.state.StateRequest");
static const cmeta_type_identity STATE_RESPONSE_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.state.StateResponse");

static const cmeta_type_desc STATE_REQUEST_TYPE = {
    "StateRequest", sizeof(StateRequest), _Alignof(StateRequest),
    CMETA_T_OBJECT, NULL, NULL, &STATE_REQUEST_ID};
static const cmeta_type_desc STATE_RESPONSE_TYPE = {
    "StateResponse", sizeof(StateResponse), _Alignof(StateResponse),
    CMETA_T_OBJECT, NULL, NULL, &STATE_RESPONSE_ID};
static const cmeta_type_desc STATE_REQUEST_PTR_TYPE = {
    "const StateRequest *", sizeof(StateRequest *), _Alignof(StateRequest *),
    CMETA_T_POINTER, &STATE_REQUEST_TYPE, NULL, NULL};
static const cmeta_type_desc STATE_RESPONSE_PTR_TYPE = {
    "StateResponse *", sizeof(StateResponse *), _Alignof(StateResponse *),
    CMETA_T_POINTER, &STATE_RESPONSE_TYPE, NULL, NULL};

static const cmeta_field_desc STATE_REQUEST_LAYOUT_FIELDS[] = {
    {"required_value", "uint32_t", offsetof(StateRequest, required_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"optional_value", "uint32_t", offsetof(StateRequest, optional_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"nullable_value", "uint32_t", offsetof(StateRequest, nullable_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"defaulted_value", "uint32_t", offsetof(StateRequest, defaulted_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL}};
static const cmeta_struct_desc STATE_REQUEST_LAYOUT = {
    "StateRequest", sizeof(StateRequest), _Alignof(StateRequest),
    STATE_REQUEST_LAYOUT_FIELDS, 4u};
static const cmeta_data_field_desc STATE_REQUEST_FIELDS[] = {
    {"test.state.StateRequest.required_value", "required_value",
     offsetof(StateRequest, required_value), &cmeta_data_uint32},
    {"test.state.StateRequest.optional_value", "optional_value",
     offsetof(StateRequest, optional_value), &cmeta_data_uint32},
    {"test.state.StateRequest.nullable_value", "nullable_value",
     offsetof(StateRequest, nullable_value), &cmeta_data_uint32},
    {"test.state.StateRequest.defaulted_value", "defaulted_value",
     offsetof(StateRequest, defaulted_value), &cmeta_data_uint32}};
static const cmeta_data_struct_shape STATE_REQUEST_SHAPE = {
    &STATE_REQUEST_LAYOUT, STATE_REQUEST_FIELDS, 4u};
static const cmeta_data_desc STATE_REQUEST_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.state.StateRequest.data",
    .display_name = "StateRequest",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &STATE_REQUEST_TYPE,
    .shape = &STATE_REQUEST_SHAPE};

static const cmeta_field_desc STATE_RESPONSE_LAYOUT_FIELDS[] = {
    {"nullable_result", "uint32_t", offsetof(StateResponse, nullable_result),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"tri_result", "uint32_t", offsetof(StateResponse, tri_result),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL}};
static const cmeta_struct_desc STATE_RESPONSE_LAYOUT = {
    "StateResponse", sizeof(StateResponse), _Alignof(StateResponse),
    STATE_RESPONSE_LAYOUT_FIELDS, 2u};
static const cmeta_data_field_desc STATE_RESPONSE_FIELDS[] = {
    {"test.state.StateResponse.nullable_result", "nullable_result",
     offsetof(StateResponse, nullable_result), &cmeta_data_uint32},
    {"test.state.StateResponse.tri_result", "tri_result",
     offsetof(StateResponse, tri_result), &cmeta_data_uint32}};
static const cmeta_data_struct_shape STATE_RESPONSE_SHAPE = {
    &STATE_RESPONSE_LAYOUT, STATE_RESPONSE_FIELDS, 2u};
static const cmeta_data_desc STATE_RESPONSE_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.state.StateResponse.data",
    .display_name = "StateResponse",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &STATE_RESPONSE_TYPE,
    .shape = &STATE_RESPONSE_SHAPE};

static const DataBindNativeStateBinding STATE_REQUEST_PRESENCE[] = {
    {sizeof(DataBindNativeStateBinding), "optional_value",
     offsetof(StateRequest, presence), 0u},
    {sizeof(DataBindNativeStateBinding), "defaulted_value",
     offsetof(StateRequest, presence), 1u}};
static const DataBindNativeStateBinding STATE_REQUEST_NULLS[] = {
    {sizeof(DataBindNativeStateBinding), "nullable_value",
     offsetof(StateRequest, nulls), 0u},
    {sizeof(DataBindNativeStateBinding), "defaulted_value",
     offsetof(StateRequest, nulls), 1u}};
static const DataBindNativeStateBinding STATE_RESPONSE_PRESENCE[] = {
    {sizeof(DataBindNativeStateBinding), "tri_result",
     offsetof(StateResponse, presence), 0u}};
static const DataBindNativeStateBinding STATE_RESPONSE_NULLS[] = {
    {sizeof(DataBindNativeStateBinding), "nullable_result",
     offsetof(StateResponse, nulls), 0u},
    {sizeof(DataBindNativeStateBinding), "tri_result",
     offsetof(StateResponse, nulls), 1u}};

static const DataBindNativeTypeBinding STATE_REQUEST_NATIVE = {
    sizeof(DataBindNativeTypeBinding),
    DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "StateRequest",
    &STATE_REQUEST_DATA,
    STATE_REQUEST_PRESENCE,
    2u,
    STATE_REQUEST_NULLS,
    2u};
static const DataBindNativeTypeBinding STATE_RESPONSE_NATIVE = {
    sizeof(DataBindNativeTypeBinding),
    DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "StateResponse",
    &STATE_RESPONSE_DATA,
    STATE_RESPONSE_PRESENCE,
    1u,
    STATE_RESPONSE_NULLS,
    2u};

FunctionDeclAs(
    value, int, &cmeta_type_int, state_run_root,
    (const StateRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &STATE_REQUEST_PTR_TYPE),
    (StateResponse *, response,
     CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, &STATE_RESPONSE_PTR_TYPE));

static DataBind *create_state_codec(void) {
  static const char schema[] =
      "message StateRequest {"
      " uint32 required_value;"
      " optional uint32 optional_value;"
      " nullable uint32 nullable_value;"
      " optional nullable uint32 defaulted_value default 7;"
      "}"
      "message StateResponse {"
      " nullable uint32 nullable_result;"
      " optional nullable uint32 tri_result;"
      "}"
      "service State { Run: StateRequest -> StateResponse; }"
      "channel StateInput: StateRequest;";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  check_equal(data_bind_create_from_text(
                  schema, sizeof(schema) - 1u, &codec, &error),
              DATA_BIND_OK);
  return codec;
}

static DataBindServiceNativeBinding state_native_binding(void) {
  return (DataBindServiceNativeBinding)
      DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
          FunctionMeta(state_run_root),
          &STATE_REQUEST_NATIVE, &STATE_RESPONSE_NATIVE);
}

typedef struct ProjectionScratch {
  char space[32];
  char name[64];
} ProjectionScratch;

static DataBindStatus http_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  ProjectionScratch *scratch = (ProjectionScratch *)context;
  (void)operation;
  (void)error;
  if (scratch == NULL || field == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (direction == DATA_BIND_BINDING_EGRESS) {
    out->binding_class = DATA_BIND_BINDING_RESULT;
    snprintf(scratch->space, sizeof(scratch->space), "http.result");
    snprintf(scratch->name, sizeof(scratch->name), "%s", field->name);
  } else if (strcmp(field->name, "right") == 0) {
    out->binding_class = DATA_BIND_BINDING_METADATA;
    snprintf(scratch->space, sizeof(scratch->space), "http.header");
    snprintf(scratch->name, sizeof(scratch->name), "X-Right");
  } else {
    out->binding_class = DATA_BIND_BINDING_VALUE;
    snprintf(scratch->space, sizeof(scratch->space), "http.query");
    snprintf(scratch->name, sizeof(scratch->name), "%s", field->name);
  }
  out->space = scratch->space;
  out->name = scratch->name;
  return DATA_BIND_OK;
}

static DataBindStatus rpc_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  ProjectionScratch *scratch = (ProjectionScratch *)context;
  (void)operation;
  (void)error;
  if (scratch == NULL || field == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  out->binding_class = direction == DATA_BIND_BINDING_INGRESS
                           ? DATA_BIND_BINDING_VALUE
                           : DATA_BIND_BINDING_RESULT;
  snprintf(scratch->space, sizeof(scratch->space), "%s",
           direction == DATA_BIND_BINDING_INGRESS
               ? "rpc.param"
               : "rpc.result");
  snprintf(scratch->name, sizeof(scratch->name), "%s", field->name);
  out->space = scratch->space;
  out->name = scratch->name;
  return DATA_BIND_OK;
}

static DataBindStatus mqtt_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  ProjectionScratch *scratch = (ProjectionScratch *)context;
  (void)operation;
  (void)error;
  if (scratch == NULL || field == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (direction == DATA_BIND_BINDING_EGRESS) {
    out->binding_class = DATA_BIND_BINDING_RESULT;
    snprintf(scratch->space, sizeof(scratch->space), "mqtt.reply");
  } else if (strcmp(field->name, "left") == 0) {
    out->binding_class = DATA_BIND_BINDING_VALUE;
    snprintf(scratch->space, sizeof(scratch->space), "mqtt.topic");
  } else if (strcmp(field->name, "right") == 0) {
    out->binding_class = DATA_BIND_BINDING_PAYLOAD;
    snprintf(scratch->space, sizeof(scratch->space), "mqtt.payload");
  } else {
    out->binding_class = DATA_BIND_BINDING_METADATA;
    snprintf(scratch->space, sizeof(scratch->space), "mqtt.property");
  }
  snprintf(scratch->name, sizeof(scratch->name), "%s", field->name);
  out->space = scratch->space;
  out->name = scratch->name;
  return DATA_BIND_OK;
}

static DataBindBindingProjection projection(
    const char *id, ProjectionScratch *scratch,
    DataBindBindingProjectFieldFn callback) {
  DataBindBindingProjection result = DATA_BIND_BINDING_PROJECTION_INIT;
  result.id = id;
  result.context = scratch;
  result.project_field = callback;
  return result;
}

typedef struct OneTokenReader {
  cserde_token token;
  int emitted;
} OneTokenReader;

static cserde_status one_token_next(void *context, cserde_token *out) {
  OneTokenReader *state = (OneTokenReader *)context;
  if (state == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (state->emitted) return CSERDE_DONE;
  *out = state->token;
  state->emitted = 1;
  return CSERDE_OK;
}

static const cserde_reader_ops ONE_TOKEN_OPS = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    one_token_next};

typedef struct TestProvider {
  OneTokenReader reader;
  int provide_scale;
  int fail_right_type;
  int fail_right_validation;
  int fail_sum_type;
  int fail_write;
  uint32_t result_sum;
  size_t begin_calls;
  size_t write_calls;
  size_t commit_calls;
  size_t abort_calls;
  uint32_t staged_sum;
  uint32_t published_sum;
} TestProvider;

static const char *entry_logical_name(const DataBindBindingPlanEntry *entry) {
  if (entry == NULL) return NULL;
  if (entry->schema_field != NULL) return entry->schema_field;
  return entry->address.name;
}

typedef struct StateProvider {
  OneTokenReader reader;
  DataBindBindingValueState required_state;
  DataBindBindingValueState optional_state;
  DataBindBindingValueState nullable_state;
  DataBindBindingValueState defaulted_state;
  uint32_t required_value;
  uint32_t optional_value;
  uint32_t nullable_value;
  uint32_t defaulted_value;
  int fail_result_type;

  DataBindBindingValueState written_required_state;
  DataBindBindingValueState written_optional_state;
  DataBindBindingValueState written_nullable_state;
  DataBindBindingValueState written_defaulted_state;
  uint32_t written_required_value;
  uint32_t written_optional_value;
  uint32_t written_nullable_value;
  uint32_t written_defaulted_value;

  size_t begin_calls;
  size_t write_calls;
  size_t value_calls;
  size_t null_calls;
  size_t absent_calls;
  size_t commit_calls;
  size_t abort_calls;
  DataBindBindingValueState nullable_result_state;
  DataBindBindingValueState tri_result_state;
  uint32_t nullable_result_value;
  uint32_t tri_result_value;
} StateProvider;

static DataBindStatus state_provider_open(
    void *context, const DataBindBindingPlanEntry *entry,
    cserde_reader *reader, DataBindBindingValueState *state,
    DataBindError *error) {
  StateProvider *provider = (StateProvider *)context;
  const char *name;
  uint32_t value = 0u;
  (void)error;

  if (provider == NULL || entry == NULL || reader == NULL || state == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  name = entry_logical_name(entry);
  if (name == NULL) return DATA_BIND_ERR_SCHEMA;

  if (strcmp(name, "required_value") == 0) {
    *state = provider->required_state;
    value = provider->required_value;
  } else if (strcmp(name, "optional_value") == 0) {
    *state = provider->optional_state;
    value = provider->optional_value;
  } else if (strcmp(name, "nullable_value") == 0) {
    *state = provider->nullable_state;
    value = provider->nullable_value;
  } else if (strcmp(name, "defaulted_value") == 0) {
    *state = provider->defaulted_state;
    value = provider->defaulted_value;
  } else if (strcmp(name, "nullable_result") == 0) {
    *state = provider->nullable_result_state;
    value = provider->nullable_result_value;
  } else if (strcmp(name, "tri_result") == 0) {
    *state = provider->tri_result_state;
    value = provider->tri_result_value;
  } else {
    return DATA_BIND_ERR_TYPE_NOT_FOUND;
  }

  if (*state != DATA_BIND_VALUE_STATE_VALUE) return DATA_BIND_OK;

  if (provider->fail_result_type &&
      (strcmp(name, "nullable_result") == 0 ||
       strcmp(name, "tri_result") == 0)) {
    static const unsigned char invalid[] = "bad";
    provider->reader.token =
        (cserde_token){.kind = CSERDE_STRING,
                       .value.slice = {invalid, 3u, CSERDE_VIEW_STABLE}};
  } else {
    provider->reader.token =
        (cserde_token){.kind = CSERDE_UINT, .value.uint = value};
  }
  provider->reader.emitted = 0;
  return cserde_reader_init(reader, &ONE_TOKEN_OPS, &provider->reader) ==
                 CSERDE_OK
             ? DATA_BIND_OK
             : DATA_BIND_ERR_RUNTIME;
}

static DataBindStatus state_provider_begin(
    void *context, DataBindError *error) {
  StateProvider *provider = (StateProvider *)context;
  (void)error;
  if (provider == NULL) return DATA_BIND_ERR_INVALID_ARG;
  ++provider->begin_calls;
  provider->write_calls = 0u;
  provider->value_calls = 0u;
  provider->null_calls = 0u;
  provider->absent_calls = 0u;
  provider->written_required_state = DATA_BIND_VALUE_STATE_ABSENT;
  provider->written_optional_state = DATA_BIND_VALUE_STATE_ABSENT;
  provider->written_nullable_state = DATA_BIND_VALUE_STATE_ABSENT;
  provider->written_defaulted_state = DATA_BIND_VALUE_STATE_ABSENT;
  provider->written_required_value = 0u;
  provider->written_optional_value = 0u;
  provider->written_nullable_value = 0u;
  provider->written_defaulted_value = 0u;
  return DATA_BIND_OK;
}

static DataBindStatus state_provider_write(
    void *context, const DataBindBindingPlanEntry *entry,
    DataBindBindingValueState state, const void *value, size_t value_bytes,
    DataBindError *error) {
  StateProvider *provider = (StateProvider *)context;
  const char *name;
  uint32_t scalar = 0u;
  (void)error;

  if (provider == NULL || entry == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (state < DATA_BIND_VALUE_STATE_ABSENT ||
      state > DATA_BIND_VALUE_STATE_NULL)
    return DATA_BIND_ERR_SCHEMA;

  if (state == DATA_BIND_VALUE_STATE_ABSENT) {
    if (value != NULL || value_bytes != 0u)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    ++provider->absent_calls;
  } else if (state == DATA_BIND_VALUE_STATE_NULL) {
    if (value != NULL || value_bytes != 0u)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    ++provider->null_calls;
  } else {
    if (value == NULL || value_bytes != sizeof(uint32_t))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    scalar = *(const uint32_t *)value;
    ++provider->value_calls;
  }

  name = entry_logical_name(entry);
  ++provider->write_calls;
  if (name != NULL && strcmp(name, "required_value") == 0) {
    provider->written_required_state = state;
    provider->written_required_value = scalar;
    return DATA_BIND_OK;
  }
  if (name != NULL && strcmp(name, "optional_value") == 0) {
    provider->written_optional_state = state;
    provider->written_optional_value = scalar;
    return DATA_BIND_OK;
  }
  if (name != NULL && strcmp(name, "nullable_value") == 0) {
    provider->written_nullable_state = state;
    provider->written_nullable_value = scalar;
    return DATA_BIND_OK;
  }
  if (name != NULL && strcmp(name, "defaulted_value") == 0) {
    provider->written_defaulted_state = state;
    provider->written_defaulted_value = scalar;
    return DATA_BIND_OK;
  }
  if (name != NULL && strcmp(name, "nullable_result") == 0) {
    provider->nullable_result_state = state;
    provider->nullable_result_value = scalar;
    return DATA_BIND_OK;
  }
  if (name != NULL && strcmp(name, "tri_result") == 0) {
    provider->tri_result_state = state;
    provider->tri_result_value = scalar;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_NOT_FOUND;
}

static DataBindStatus state_provider_commit(
    void *context, DataBindError *error) {
  StateProvider *provider = (StateProvider *)context;
  (void)error;
  if (provider == NULL) return DATA_BIND_ERR_INVALID_ARG;
  ++provider->commit_calls;
  return DATA_BIND_OK;
}

static void state_provider_abort(void *context) {
  StateProvider *provider = (StateProvider *)context;
  if (provider != NULL) ++provider->abort_calls;
}

static DataBindBindingProvider state_provider_for(StateProvider *state) {
  DataBindBindingProvider provider = DATA_BIND_BINDING_PROVIDER_INIT;
  provider.context = state;
  provider.open_input = state_provider_open;
  provider.begin_output = state_provider_begin;
  provider.write_output = state_provider_write;
  provider.commit_output = state_provider_commit;
  provider.abort_output = state_provider_abort;
  return provider;
}


static DataBindStatus provider_open(
    void *context, const DataBindBindingPlanEntry *entry,
    cserde_reader *reader, DataBindBindingValueState *state,
    DataBindError *error) {
  TestProvider *provider = (TestProvider *)context;
  const char *name;
  uint64_t value;
  (void)error;

  if (provider == NULL || entry == NULL || reader == NULL || state == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (entry->address.binding_class != DATA_BIND_BINDING_VALUE &&
      entry->address.binding_class != DATA_BIND_BINDING_METADATA &&
      entry->address.binding_class != DATA_BIND_BINDING_PAYLOAD &&
      entry->address.binding_class != DATA_BIND_BINDING_RESULT &&
      entry->address.binding_class != DATA_BIND_BINDING_ERROR)
    return DATA_BIND_ERR_SCHEMA;

  name = entry_logical_name(entry);
  *state = DATA_BIND_VALUE_STATE_VALUE;
  if (strcmp(name, "left") == 0) {
    value = 3u;
  } else if (strcmp(name, "right") == 0) {
    if (provider->fail_right_type) {
      static const unsigned char invalid[] = "bad";
      provider->reader.token =
          (cserde_token){.kind = CSERDE_STRING,
                         .value.slice = {invalid, 3u, CSERDE_VIEW_STABLE}};
      provider->reader.emitted = 0;
      return cserde_reader_init(reader, &ONE_TOKEN_OPS, &provider->reader) ==
                     CSERDE_OK
                 ? DATA_BIND_OK
                 : DATA_BIND_ERR_RUNTIME;
    }
    value = provider->fail_right_validation ? 99u : 4u;
  } else if (strcmp(name, "scale") == 0) {
    if (!provider->provide_scale) {
      *state = DATA_BIND_VALUE_STATE_ABSENT;
      return DATA_BIND_OK;
    }
    value = 2u;
  } else if (strcmp(name, "sum") == 0) {
    value = provider->result_sum;
    if (provider->fail_sum_type) {
      static const unsigned char invalid[] = "bad";
      provider->reader.token =
          (cserde_token){.kind = CSERDE_STRING,
                         .value.slice = {invalid, 3u, CSERDE_VIEW_STABLE}};
      provider->reader.emitted = 0;
      return cserde_reader_init(reader, &ONE_TOKEN_OPS, &provider->reader) ==
                     CSERDE_OK
                 ? DATA_BIND_OK
                 : DATA_BIND_ERR_RUNTIME;
    }
  } else {
    return DATA_BIND_ERR_TYPE_NOT_FOUND;
  }

  provider->reader.token =
      (cserde_token){.kind = CSERDE_UINT, .value.uint = value};
  provider->reader.emitted = 0;
  return cserde_reader_init(reader, &ONE_TOKEN_OPS, &provider->reader) ==
                 CSERDE_OK
             ? DATA_BIND_OK
             : DATA_BIND_ERR_RUNTIME;
}

static DataBindStatus provider_begin(void *context, DataBindError *error) {
  TestProvider *provider = (TestProvider *)context;
  (void)error;
  ++provider->begin_calls;
  provider->staged_sum = 0u;
  return DATA_BIND_OK;
}

static DataBindStatus provider_write(
    void *context, const DataBindBindingPlanEntry *entry,
    DataBindBindingValueState state,
    const void *value, size_t value_bytes, DataBindError *error) {
  TestProvider *provider = (TestProvider *)context;
  (void)error;
  ++provider->write_calls;
  if (provider->fail_write) return DATA_BIND_ERR_RUNTIME;
  if (state != DATA_BIND_VALUE_STATE_VALUE ||
      entry->address.binding_class != DATA_BIND_BINDING_RESULT ||
      strcmp(entry_logical_name(entry), "sum") != 0 ||
      value == NULL || value_bytes != sizeof(uint32_t))
    return DATA_BIND_ERR_TYPE_MISMATCH;
  provider->staged_sum = *(const uint32_t *)value;
  return DATA_BIND_OK;
}

static DataBindStatus provider_commit(void *context, DataBindError *error) {
  TestProvider *provider = (TestProvider *)context;
  (void)error;
  ++provider->commit_calls;
  provider->published_sum = provider->staged_sum;
  return DATA_BIND_OK;
}

static void provider_abort(void *context) {
  TestProvider *provider = (TestProvider *)context;
  ++provider->abort_calls;
  provider->staged_sum = 0u;
}

static DataBindBindingProvider provider_for(TestProvider *state) {
  DataBindBindingProvider provider = DATA_BIND_BINDING_PROVIDER_INIT;
  provider.context = state;
  provider.open_input = provider_open;
  provider.begin_output = provider_begin;
  provider.write_output = provider_write;
  provider.commit_output = provider_commit;
  provider.abort_output = provider_abort;
  return provider;
}


typedef struct EncodeWriterContext {
  cserde_token token;
  size_t write_calls;
} EncodeWriterContext;

typedef struct EncodeOutputProvider {
  DataBindNativeOptions *options;
  DataBindNativeDiagnostic native_diagnostic;
  EncodeWriterContext writer_context;
  cserde_writer writer;
  size_t begin_calls;
  size_t write_calls;
  size_t commit_calls;
  size_t abort_calls;
  uint32_t published_sum;
} EncodeOutputProvider;

static cserde_status encode_writer_write(
    void *context, const cserde_token *token) {
  EncodeWriterContext *writer = (EncodeWriterContext *)context;
  if (writer == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  ++writer->write_calls;
  writer->token = *token;
  return CSERDE_OK;
}

static cserde_status encode_writer_finish(void *context) {
  (void)context;
  return CSERDE_OK;
}

static const cserde_writer_ops ENCODE_WRITER_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    encode_writer_write, encode_writer_finish};

static DataBindStatus encode_provider_begin(
    void *context, DataBindError *error) {
  EncodeOutputProvider *provider = (EncodeOutputProvider *)context;
  (void)error;
  if (provider == NULL || provider->options == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  ++provider->begin_calls;
  provider->writer_context = (EncodeWriterContext){0};
  provider->writer = (cserde_writer){0};
  provider->native_diagnostic =
      (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  return cserde_writer_init(
             &provider->writer, &ENCODE_WRITER_OPS,
             &provider->writer_context) == CSERDE_OK
             ? DATA_BIND_OK
             : DATA_BIND_ERR_RUNTIME;
}

static DataBindStatus encode_provider_write(
    void *context, const DataBindBindingPlanEntry *entry,
    DataBindBindingValueState state,
    const void *value, size_t value_bytes, DataBindError *error) {
  EncodeOutputProvider *provider = (EncodeOutputProvider *)context;
  DataBindStatus status;
  if (state != DATA_BIND_VALUE_STATE_VALUE || value == NULL)
    return DATA_BIND_ERR_TYPE_MISMATCH;
  ++provider->write_calls;
  status = data_bind_native_encode(
      provider->options, entry->data, value, value_bytes,
      &provider->writer, &provider->native_diagnostic);
  if (status != DATA_BIND_OK && error != NULL)
    *error = provider->native_diagnostic.error;
  return status;
}

static DataBindStatus encode_provider_commit(
    void *context, DataBindError *error) {
  EncodeOutputProvider *provider = (EncodeOutputProvider *)context;
  (void)error;
  ++provider->commit_calls;
  if (provider->writer_context.write_calls != 1u ||
      provider->writer_context.token.kind != CSERDE_UINT ||
      provider->writer.state != CSERDE_WRITER_READY)
    return DATA_BIND_ERR_TYPE_MISMATCH;
  provider->published_sum =
      (uint32_t)provider->writer_context.token.value.uint;
  return DATA_BIND_OK;
}

static void encode_provider_abort(void *context) {
  EncodeOutputProvider *provider = (EncodeOutputProvider *)context;
  ++provider->abort_calls;
  provider->published_sum = 0u;
}

static DataBindBindingProvider encode_provider_for(
    EncodeOutputProvider *state) {
  DataBindBindingProvider provider = DATA_BIND_BINDING_PROVIDER_INIT;
  provider.context = state;
  provider.begin_output = encode_provider_begin;
  provider.write_output = encode_provider_write;
  provider.commit_output = encode_provider_commit;
  provider.abort_output = encode_provider_abort;
  return provider;
}

static DataBindNativeOptions native_options(
    unsigned char *workspace, size_t workspace_bytes) {
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  options.workspace = workspace;
  options.workspace_bytes = workspace_bytes;
  options.max_depth = 16u;
  options.max_items = 64u;
  options.max_owned_bytes = 1024u;
  return options;
}

typedef struct MessageTokenReader {
  const cserde_token *tokens;
  size_t count;
  size_t index;
} MessageTokenReader;

static cserde_status message_token_next(
    void *context, cserde_token *out) {
  MessageTokenReader *state = (MessageTokenReader *)context;
  if (state == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (state->index >= state->count) return CSERDE_DONE;
  *out = state->tokens[state->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops MESSAGE_TOKEN_READER_OPS = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    message_token_next};

typedef struct MessageTokenWriter {
  cserde_token tokens[32];
  size_t count;
} MessageTokenWriter;

static cserde_status message_token_write(
    void *context, const cserde_token *token) {
  MessageTokenWriter *state = (MessageTokenWriter *)context;
  if (state == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  if (state->count >= sizeof(state->tokens) / sizeof(state->tokens[0]))
    return CSERDE_LIMIT_EXCEEDED;
  state->tokens[state->count++] = *token;
  return CSERDE_OK;
}

static cserde_status message_token_finish(void *context) {
  return context != NULL ? CSERDE_OK : CSERDE_INVALID_ARGUMENT;
}

static const cserde_writer_ops MESSAGE_TOKEN_WRITER_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    message_token_write, message_token_finish};

static int message_writer_init(
    cserde_writer *writer, MessageTokenWriter *state) {
  if (writer == NULL || state == NULL) return 0;
  *writer = (cserde_writer){0};
  *state = (MessageTokenWriter){0};
  return cserde_writer_init(
             writer, &MESSAGE_TOKEN_WRITER_OPS, state) == CSERDE_OK;
}

static int token_string_equal(
    const cserde_token *token, const char *text) {
  size_t length = text != NULL ? strlen(text) : 0u;
  return token != NULL && token->kind == CSERDE_STRING &&
         token->value.slice.size == length &&
         (length == 0u ||
          (token->value.slice.data != NULL &&
           memcmp(token->value.slice.data, text, length) == 0));
}

static cserde_token message_key(const char *text) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data = (const unsigned char *)text;
  token.value.slice.size = strlen(text);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

static int message_reader_init(
    cserde_reader *reader,
    MessageTokenReader *state,
    const cserde_token *tokens,
    size_t count) {
  if (reader == NULL || state == NULL || tokens == NULL) return 0;
  /*
   * Each fixture models a fresh format reader. MessagePlan intentionally
   * consumes one value without probing EOF, so a successfully consumed reader
   * remains READY and must not be reinitialized in place.
   */
  *reader = (cserde_reader){0};
  state->tokens = tokens;
  state->count = count;
  state->index = 0u;
  return cserde_reader_init(
             reader, &MESSAGE_TOKEN_READER_OPS, state) == CSERDE_OK;
}

spec("DataBind canonical Service BindingPlan") {
  it("encodes native MessagePlan ABSENT NULL and VALUE state canonically") {
    DataBind *codec = create_state_codec();
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    unsigned char workspace[1024] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    StateRequest value = {0};
    MessageTokenWriter sink = {0};
    cserde_writer writer = {0};

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "StateRequest", &STATE_REQUEST_NATIVE,
            &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    value.required_value = 9u;
    value.optional_value = 31u;
    value.nullable_value = 41u;
    value.defaulted_value = 7u;
    value.presence = (uint8_t)(1u << 1);
    value.nulls = (uint8_t)(1u << 0);

    check_true(message_writer_init(&writer, &sink));
    check_equal(
        data_bind_message_plan_encode_native(
            plan, &options, &value, sizeof(value), &writer, &diagnostic),
        DATA_BIND_OK);
    check_equal(sink.count, (size_t)8u);
    check_equal(sink.tokens[0].kind, CSERDE_MAP_BEGIN);
    check_true(token_string_equal(&sink.tokens[1], "required_value"));
    check_equal(sink.tokens[2].kind, CSERDE_UINT);
    check_equal(sink.tokens[2].value.uint, UINT64_C(9));
    check_true(token_string_equal(&sink.tokens[3], "nullable_value"));
    check_equal(sink.tokens[4].kind, CSERDE_NULL);
    check_true(token_string_equal(&sink.tokens[5], "defaulted_value"));
    check_equal(sink.tokens[6].kind, CSERDE_UINT);
    check_equal(sink.tokens[6].value.uint, UINT64_C(7));
    check_equal(sink.tokens[7].kind, CSERDE_MAP_END);

    value.optional_value = 3u;
    value.nullable_value = 5u;
    value.defaulted_value = 99u;
    value.presence = (uint8_t)((1u << 0) | (1u << 1));
    value.nulls = (uint8_t)(1u << 1);
    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_writer_init(&writer, &sink));
    check_equal(
        data_bind_message_plan_encode_native(
            plan, &options, &value, sizeof(value), &writer, &diagnostic),
        DATA_BIND_OK);
    check_equal(sink.count, (size_t)10u);
    check_true(token_string_equal(&sink.tokens[1], "required_value"));
    check_true(token_string_equal(&sink.tokens[3], "optional_value"));
    check_equal(sink.tokens[4].kind, CSERDE_UINT);
    check_equal(sink.tokens[4].value.uint, UINT64_C(3));
    check_true(token_string_equal(&sink.tokens[5], "nullable_value"));
    check_equal(sink.tokens[6].kind, CSERDE_UINT);
    check_equal(sink.tokens[6].value.uint, UINT64_C(5));
    check_true(token_string_equal(&sink.tokens[7], "defaulted_value"));
    check_equal(sink.tokens[8].kind, CSERDE_NULL);
    check_equal(sink.tokens[9].kind, CSERDE_MAP_END);

    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("validates native MessagePlan fields before encoding their values") {
    DataBind *codec = create_codec();
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    unsigned char workspace[1024] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    AddRequest invalid = {0};
    MessageTokenWriter sink = {0};
    cserde_writer writer = {0};

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "AddRequest", &ADD_REQUEST_NATIVE,
            &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    invalid.left = 0u;
    invalid.right = 4u;
    invalid.scale = 0u;
    invalid.presence = 0u;
    check_true(message_writer_init(&writer, &sink));
    check_equal(
        data_bind_message_plan_encode_native(
            plan, &options, &invalid, sizeof(invalid), &writer, &diagnostic),
        DATA_BIND_ERR_VALIDATION);
    check_equal(diagnostic.schema_field, "left");
    check_equal(sink.count, (size_t)2u);
    check_equal(sink.tokens[0].kind, CSERDE_MAP_BEGIN);
    check_true(token_string_equal(&sink.tokens[1], "left"));

    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("coerces CSV and XML text scalars only through format-aware native decode") {
    DataBind *codec = create_state_codec();
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    unsigned char workspace[1024] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    StateRequest xml_value = {0};
    StateRequest strict_value = {0};
    MessageTokenReader source = {0};
    cserde_reader reader = {0};
    const cserde_token xml_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("required_value"),
        message_key("9"),
        message_key("nullable_value"),
        message_key("41"),
        {.kind = CSERDE_MAP_END},
    };

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "StateRequest", &STATE_REQUEST_NATIVE,
            &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    check_true(message_reader_init(
        &reader, &source, xml_tokens,
        sizeof(xml_tokens) / sizeof(xml_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native_format(
            plan, &options, DATA_BIND_FORMAT_XML, &reader,
            &xml_value, sizeof(xml_value), &diagnostic),
        DATA_BIND_OK);
    check_equal(xml_value.required_value, UINT32_C(9));
    check_equal(xml_value.optional_value, UINT32_C(0));
    check_equal(xml_value.nullable_value, UINT32_C(41));
    check_equal(xml_value.defaulted_value, UINT32_C(7));
    check_equal(xml_value.presence, (uint8_t)(1u << 1));
    check_equal(xml_value.nulls, (uint8_t)0u);

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_reader_init(
        &reader, &source, xml_tokens,
        sizeof(xml_tokens) / sizeof(xml_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &strict_value, sizeof(strict_value), &diagnostic),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(strict_value.required_value, UINT32_C(0));
    check_equal(strict_value.optional_value, UINT32_C(0));
    check_equal(strict_value.nullable_value, UINT32_C(0));
    check_equal(strict_value.defaulted_value, UINT32_C(0));
    check_equal(strict_value.presence, (uint8_t)0u);
    check_equal(strict_value.nulls, (uint8_t)0u);

    strict_value = (StateRequest){0};
    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_reader_init(
        &reader, &source, xml_tokens,
        sizeof(xml_tokens) / sizeof(xml_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native_format(
            plan, &options, DATA_BIND_FORMAT_CSV, &reader,
            &strict_value, sizeof(strict_value), &diagnostic),
        DATA_BIND_OK);
    check_equal(strict_value.required_value, UINT32_C(9));
    check_equal(strict_value.optional_value, UINT32_C(0));
    check_equal(strict_value.nullable_value, UINT32_C(41));
    check_equal(strict_value.defaulted_value, UINT32_C(7));
    check_equal(strict_value.presence, (uint8_t)(1u << 1));
    check_equal(strict_value.nulls, (uint8_t)0u);

    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("decodes one Channel message with optional nullable and default state") {
    DataBind *codec = create_state_codec();
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    unsigned char workspace[1024] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    StateRequest value = {0};
    StateRequest explicit_null = {0};
    MessageTokenReader source = {0};
    cserde_reader reader = {0};
    const cserde_token tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("required_value"),
        {.kind = CSERDE_UINT, .value.uint = 9u},
        message_key("nullable_value"),
        {.kind = CSERDE_NULL},
        {.kind = CSERDE_MAP_END},
        {.kind = CSERDE_UINT, .value.uint = 99u},
    };
    const cserde_token explicit_null_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("required_value"),
        {.kind = CSERDE_UINT, .value.uint = 8u},
        message_key("nullable_value"),
        {.kind = CSERDE_UINT, .value.uint = 5u},
        message_key("defaulted_value"),
        {.kind = CSERDE_NULL},
        {.kind = CSERDE_MAP_END},
    };

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "StateRequest", &STATE_REQUEST_NATIVE,
            &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    check_true(message_reader_init(
        &reader, &source, tokens,
        sizeof(tokens) / sizeof(tokens[0])));

    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &value, sizeof(value), &diagnostic),
        DATA_BIND_OK);
    check_equal(value.required_value, (uint32_t)9u);
    check_equal(value.optional_value, (uint32_t)0u);
    check_equal(value.nullable_value, (uint32_t)0u);
    check_equal(value.defaulted_value, (uint32_t)7u);
    check_equal(value.presence, (uint8_t)(1u << 1u));
    check_equal(value.nulls, (uint8_t)(1u << 0u));

    /* One message is consumed; the next token is deliberately untouched. */
    check_equal(source.index, (size_t)6u);

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_reader_init(
        &reader, &source, explicit_null_tokens,
        sizeof(explicit_null_tokens) / sizeof(explicit_null_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &explicit_null, sizeof(explicit_null), &diagnostic),
        DATA_BIND_OK);
    check_equal(explicit_null.required_value, (uint32_t)8u);
    check_equal(explicit_null.optional_value, (uint32_t)0u);
    check_equal(explicit_null.nullable_value, (uint32_t)5u);
    check_equal(explicit_null.defaulted_value, (uint32_t)0u);
    check_equal(explicit_null.presence, (uint8_t)(1u << 1u));
    check_equal(explicit_null.nulls, (uint8_t)(1u << 1u));

    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("applies defaults validates values and rolls failed messages to zero") {
    DataBind *codec = create_codec();
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    unsigned char workspace[1024] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    AddRequest valid = {0};
    AddRequest invalid = {0};
    AddRequest duplicate = {0};
    AddRequest unknown = {0};
    AddRequest missing = {0};
    AddRequest invalid_null = {0};
    MessageTokenReader source = {0};
    cserde_reader reader = {0};

    const cserde_token valid_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("left"),
        {.kind = CSERDE_UINT, .value.uint = 3u},
        message_key("right"),
        {.kind = CSERDE_UINT, .value.uint = 4u},
        {.kind = CSERDE_MAP_END},
    };
    const cserde_token invalid_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("left"),
        {.kind = CSERDE_UINT, .value.uint = 0u},
        message_key("right"),
        {.kind = CSERDE_UINT, .value.uint = 4u},
        {.kind = CSERDE_MAP_END},
    };
    const cserde_token duplicate_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("left"),
        {.kind = CSERDE_UINT, .value.uint = 3u},
        message_key("left"),
        {.kind = CSERDE_UINT, .value.uint = 4u},
        {.kind = CSERDE_MAP_END},
    };
    const cserde_token unknown_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("left"),
        {.kind = CSERDE_UINT, .value.uint = 3u},
        message_key("vendor"),
        {.kind = CSERDE_UINT, .value.uint = 4u},
        {.kind = CSERDE_MAP_END},
    };
    const cserde_token missing_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("left"),
        {.kind = CSERDE_UINT, .value.uint = 3u},
        {.kind = CSERDE_MAP_END},
    };
    const cserde_token null_tokens[] = {
        {.kind = CSERDE_MAP_BEGIN},
        message_key("left"),
        {.kind = CSERDE_UINT, .value.uint = 3u},
        message_key("right"),
        {.kind = CSERDE_UINT, .value.uint = 4u},
        message_key("scale"),
        {.kind = CSERDE_NULL},
        {.kind = CSERDE_MAP_END},
    };

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "AddRequest", &ADD_REQUEST_NATIVE,
            &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);

    check_true(message_reader_init(
        &reader, &source, valid_tokens,
        sizeof(valid_tokens) / sizeof(valid_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &valid, sizeof(valid), &diagnostic),
        DATA_BIND_OK);
    check_equal(valid.left, (uint32_t)3u);
    check_equal(valid.right, (uint32_t)4u);
    check_equal(valid.scale, (uint32_t)1u);
    check_equal(valid.presence, (uint8_t)1u);

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_reader_init(
        &reader, &source, invalid_tokens,
        sizeof(invalid_tokens) / sizeof(invalid_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &invalid, sizeof(invalid), &diagnostic),
        DATA_BIND_ERR_VALIDATION);
    check_equal(invalid.left, (uint32_t)0u);
    check_equal(invalid.right, (uint32_t)0u);
    check_equal(invalid.scale, (uint32_t)0u);
    check_equal(invalid.presence, (uint8_t)0u);
    check_equal(diagnostic.schema_field, "left");

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_reader_init(
        &reader, &source, duplicate_tokens,
        sizeof(duplicate_tokens) / sizeof(duplicate_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &duplicate, sizeof(duplicate), &diagnostic),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(duplicate.left, (uint32_t)0u);
    check_equal(duplicate.right, (uint32_t)0u);
    check_equal(duplicate.scale, (uint32_t)0u);
    check_equal(duplicate.presence, (uint8_t)0u);
    check_contains(diagnostic.message, "Duplicate");

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_reader_init(
        &reader, &source, unknown_tokens,
        sizeof(unknown_tokens) / sizeof(unknown_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &unknown, sizeof(unknown), &diagnostic),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(unknown.left, (uint32_t)0u);
    check_equal(unknown.right, (uint32_t)0u);
    check_equal(unknown.scale, (uint32_t)0u);
    check_equal(unknown.presence, (uint8_t)0u);
    check_contains(diagnostic.message, "Unknown");

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_reader_init(
        &reader, &source, missing_tokens,
        sizeof(missing_tokens) / sizeof(missing_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &missing, sizeof(missing), &diagnostic),
        DATA_BIND_ERR_TYPE_NOT_FOUND);
    check_equal(missing.left, (uint32_t)0u);
    check_equal(missing.right, (uint32_t)0u);
    check_equal(missing.scale, (uint32_t)0u);
    check_equal(missing.presence, (uint8_t)0u);
    check_equal(diagnostic.schema_field, "right");

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_true(message_reader_init(
        &reader, &source, null_tokens,
        sizeof(null_tokens) / sizeof(null_tokens[0])));
    check_equal(
        data_bind_message_plan_decode_native(
            plan, &options, &reader,
            &invalid_null, sizeof(invalid_null), &diagnostic),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(invalid_null.left, (uint32_t)0u);
    check_equal(invalid_null.right, (uint32_t)0u);
    check_equal(invalid_null.scale, (uint32_t)0u);
    check_equal(invalid_null.presence, (uint8_t)0u);
    check_equal(diagnostic.schema_field, "scale");

    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("compiles and validates one Channel message without FunctionDesc") {
    DataBind *codec = create_codec();
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    AddRequest value = {0};

    check_not_null(codec);
    if (codec == NULL) return;

    check_equal(
        data_bind_message_plan_compile(
            codec, "AddRequest", &ADD_REQUEST_NATIVE, &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    check_equal(data_bind_message_plan_type_name(plan), "AddRequest");
    check_true(data_bind_message_plan_native_binding(plan) !=
               &ADD_REQUEST_NATIVE);
    check_equal(data_bind_message_plan_native_binding(plan)->idl_type_name,
                ADD_REQUEST_NATIVE.idl_type_name);
    check_true(cmeta_data_desc_equal(
        data_bind_message_plan_native_binding(plan)->data, ADD_REQUEST_NATIVE.data));
    check_equal(data_bind_message_plan_field_count(plan), (size_t)3u);

    value.left = 2u;
    value.right = 3u;
    value.scale = 0u;
    value.presence = 0u;
    check_equal(
        data_bind_message_plan_validate_native(
            plan, &value, sizeof(value), &error),
        DATA_BIND_OK);

    value.left = 0u;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_message_plan_validate_native(
            plan, &value, sizeof(value), &error),
        DATA_BIND_ERR_VALIDATION);
    check_contains(error.path, "left");

    value.left = 2u;
    value.scale = 4u;
    value.presence = 1u;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_message_plan_validate_native(
            plan, &value, sizeof(value), &error),
        DATA_BIND_ERR_VALIDATION);
    check_contains(error.path, "scale");

    value.presence = 0u;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_message_plan_validate_native(
            plan, &value, sizeof(value), &error),
        DATA_BIND_OK);

    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("compiles HTTP RPC and MQTT through the same generic provider ABI") {
    DataBind *codec = create_codec();
    ProjectionScratch http_scratch = {{0}, {0}};
    ProjectionScratch rpc_scratch = {{0}, {0}};
    ProjectionScratch mqtt_scratch = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &http_scratch, http_project);
    DataBindBindingProjection rpc =
        projection("rpc-v1", &rpc_scratch, rpc_project);
    DataBindBindingProjection mqtt =
        projection("mqtt-v1", &mqtt_scratch, mqtt_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *http_plan = NULL;
    DataBindBindingPlan *rpc_plan = NULL;
    DataBindBindingPlan *mqtt_plan = NULL;
    DataBindBindingPlanEntry entry = DATA_BIND_BINDING_PLAN_ENTRY_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &http_plan, &diagnostic),
                DATA_BIND_OK);
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc, &native,
                    &rpc_plan, &diagnostic),
                DATA_BIND_OK);
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &mqtt, &native,
                    &mqtt_plan, &diagnostic),
                DATA_BIND_OK);

    check_equal(data_bind_binding_plan_operation_id(http_plan), "Calc.Add");
    check_equal(data_bind_binding_plan_projection_id(http_plan), "http-v1");
    check_equal(data_bind_binding_plan_error_count(http_plan), (size_t)0u);
    check_null(data_bind_binding_plan_error_at(http_plan, 0u));

    check(data_bind_binding_plan_ingress_at(http_plan, 1u, &entry) == 1);
    check_true(entry.address.binding_class == DATA_BIND_BINDING_METADATA);
    check_equal(entry.address.space, "http.header");
    check_equal(entry.address.name, "X-Right");

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_ingress_at(rpc_plan, 0u, &entry) == 1);
    check_true(entry.address.binding_class == DATA_BIND_BINDING_VALUE);
    check_equal(entry.address.space, "rpc.param");

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_ingress_at(mqtt_plan, 1u, &entry) == 1);
    check_true(entry.address.binding_class == DATA_BIND_BINDING_PAYLOAD);
    check_equal(entry.address.space, "mqtt.payload");

    data_bind_binding_plan_free(mqtt_plan);
    data_bind_binding_plan_free(rpc_plan);
    data_bind_binding_plan_free(http_plan);
    data_bind_free(codec);
  }

  it("treats a reflected receiver as invocation context, not schema storage") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-receiver-v1", &scratch, http_project);
    const cmeta_function_desc *function = FunctionMeta(calc_add_receiver_root);
    DataBindServiceNativeBinding native = native_binding(function);
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    DataBindBindingPlanEntry entry = DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    const cmeta_param_desc *receiver = cmeta_function_receiver(function);
    size_t i;

    check_not_null(receiver);
    check_true(receiver == cmeta_function_param(function, 0u));
    check_equal(receiver->name, "self");
    check_true((receiver->flags & CMETA_PARAM_RECEIVER) != 0u);
    check_true(cmeta_type_equal(
        receiver->type->pointee, &ADD_REQUEST_TYPE));

    /*
     * Receiver and request deliberately share AddRequest as their pointee.
     * Without receiver exclusion, root-type inference is ambiguous.
     */
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    check_true(data_bind_binding_plan_function(plan) == function);
    check_equal(data_bind_binding_plan_ingress_count(plan), (size_t)3u);
    check_equal(data_bind_binding_plan_egress_count(plan), (size_t)1u);

    for (i = 0u; i < data_bind_binding_plan_ingress_count(plan); ++i) {
      entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
      check_true(data_bind_binding_plan_ingress_at(plan, i, &entry) == 1);
      check_equal(entry.function_param_index, (size_t)1u);
      check_true(entry.function_param_index != 0u);
    }

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check_true(data_bind_binding_plan_egress_at(plan, 0u, &entry) == 1);
    check_equal(entry.function_param_index, (size_t)2u);
    check_true(entry.function_param_index != 0u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("terminates deferred failed invocations cancellation and rejected admission once") {
    enum { CALL_EXECUTE_FAILURE, CALL_CANCEL_PENDING, CALL_REJECT_ADMISSION, CALL_MODE_COUNT };
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http = projection("http-deferred-call", &scratch, http_project);
    DataBindServiceNativeBinding native = native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic = DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {0};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options = native_options(workspace, sizeof(workspace));
    unsigned mode;
    check_equal(data_bind_binding_plan_compile_service(
        codec, "Calc", "Add", &http, &native, &plan, &diagnostic), DATA_BIND_OK);
    data_bind_free(codec);
    for (mode = 0u; mode < CALL_MODE_COUNT; ++mode) {
      cflow_executor executor = {0};
      cflow_executor_control control = {0};
      AddRequest request = {0};
      AddResponse response = {0};
      void *params[] = {&request, &response};
      const size_t param_bytes[] = {sizeof(request), sizeof(response)};
      DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
      DataBindBindingCallLifetime lifetime = DATA_BIND_BINDING_CALL_LIFETIME_INIT;
      DeferredCallProbe probe = {.lifetime = &lifetime, .response = &response};
      cflow_executor_task task = {
          .run = deferred_call_run, .cancel = deferred_call_cancel,
          .finalize = deferred_call_finalize, .user = &probe};
      frame.request = &request;
      frame.request_bytes = sizeof(request);
      frame.params = params;
      frame.param_bytes = param_bytes;
      frame.param_count = 2u;
      check_true(cflow_executor_manual_init_with_capacity(&executor, 1u));
      check_true(cflow_executor_as_control(&executor, &control));
      check_equal(data_bind_binding_plan_bind_call(
          plan, &provider, &options, &frame, &lifetime, &diagnostic), DATA_BIND_OK);
      if (mode == CALL_REJECT_ADMISSION) {
        check_true(cflow_executor_control_shutdown(&control, CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check_equal(cflow_executor_try_post_task(&executor, &task), CFLOW_ADMISSION_CLOSED);
        check_true(data_bind_binding_call_is_live(&lifetime));
        check_equal(probe.finalize_calls, 0u);
        deferred_call_finalize(&probe);
      } else {
        check_equal(cflow_executor_try_post_task(&executor, &task), CFLOW_ADMISSION_ACCEPTED);
        check_true(data_bind_binding_call_is_live(&lifetime));
        if (mode == CALL_CANCEL_PENDING)
          check_true(cflow_executor_control_shutdown(&control, CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        else {
          check_equal(cflow_executor_run_ready(&executor), (size_t)1u);
          check_equal(probe.native_status, -1);
        }
      }
      check_equal(probe.run_calls, mode == CALL_EXECUTE_FAILURE ? 1u : 0u);
      check_equal(probe.cancel_calls, mode == CALL_CANCEL_PENDING ? 1u : 0u);
      check_equal(probe.finalize_calls, 1u);
      check_equal(probe.restore_status, DATA_BIND_OK);
      check_false(data_bind_binding_call_is_live(&lifetime));
      check_equal(request.left, 0u);
      check_equal(request.presence, 0u);
      check_equal(response.sum, 0u);
      cflow_executor_destroy(&executor);
      check_equal(probe.finalize_calls, 1u);
    }
    data_bind_binding_plan_free(plan);
  }

  it("tracks a whole call through egress failure and rejects repeated teardown") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http = projection("http-call", &scratch, http_project);
    DataBindServiceNativeBinding native = native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic = DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {.fail_write = 1};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options = native_options(workspace, sizeof(workspace));
    AddRequest request = {0};
    AddResponse response = {0};
    void *params[] = {&request, &response};
    const size_t param_bytes[] = {sizeof(request), sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
    DataBindBindingCallLifetime lifetime = DATA_BIND_BINDING_CALL_LIFETIME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
        codec, "Calc", "Add", &http, &native, &plan, &diagnostic), DATA_BIND_OK);
    data_bind_free(codec);
    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 2u;
    check_equal(data_bind_binding_plan_bind_call(
        plan, &provider, &options, &frame, &lifetime, &diagnostic), DATA_BIND_OK);
    check_true(data_bind_binding_call_is_live(&lifetime));
    response.sum = 7u;
    check_equal(data_bind_binding_plan_bind_call(
        plan, &provider, &options, &frame, &lifetime, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
    check_equal(response.sum, 7u);
    check_equal(data_bind_binding_plan_write_outputs(
        plan, &provider, &frame, &diagnostic), DATA_BIND_ERR_RUNTIME);
    check_true(data_bind_binding_call_is_live(&lifetime));
    options.workspace = NULL;
    options.workspace_bytes = 0u;
    check_equal(data_bind_binding_call_restore_zero(&lifetime, &diagnostic), DATA_BIND_OK);
    check_equal(request.left, 0u);
    check_equal(request.presence, 0u);
    check_equal(response.sum, 0u);
    check_false(data_bind_binding_call_is_live(&lifetime));
    check_equal(data_bind_binding_call_restore_zero(
        &lifetime, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
    data_bind_binding_plan_free(plan);
  }

  it("initializes and rolls back by-value response staging with the whole call") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http = projection("http-return", &scratch, http_project);
    DataBindServiceNativeBinding native = native_binding(FunctionMeta(calc_value_return));
    DataBindBindingPlanDiagnostic diagnostic = DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {.fail_right_type = 1};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options = native_options(workspace, sizeof(workspace));
    AddRequest request = {0};
    AddResponse response = {.sum = 99u};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
    DataBindBindingCallLifetime lifetime = DATA_BIND_BINDING_CALL_LIFETIME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
        codec, "Calc", "Add", &http, &native, &plan, &diagnostic), DATA_BIND_OK);
    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.return_value = &response;
    frame.return_bytes = sizeof(response);
    frame.param_count = 1u;
    check_equal(data_bind_binding_plan_bind_call(
        plan, &provider, &options, &frame, &lifetime, &diagnostic), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(request.left, 0u);
    check_equal(request.presence, 0u);
    check_equal(response.sum, 0u);
    check_false(data_bind_binding_call_is_live(&lifetime));
    state.fail_right_type = 0;
    check_equal(data_bind_binding_plan_bind_call(
        plan, &provider, &options, &frame, &lifetime, &diagnostic), DATA_BIND_OK);
    response.sum = 7u;
    check_equal(data_bind_binding_plan_write_outputs(
        plan, &provider, &frame, &diagnostic), DATA_BIND_OK);
    check_equal(state.published_sum, 7u);
    check_equal(data_bind_binding_call_restore_zero(&lifetime, &diagnostic), DATA_BIND_OK);
    check_equal(response.sum, 0u);
    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("executes a compiled root plan after the schema codec is released") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &scratch, http_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {0};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    AddRequest request = {0};
    AddResponse response = {0};
    void *params[] = {NULL, &response};
    const size_t param_bytes[] = {0u, sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    data_bind_free(codec);
    codec = NULL;

    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 2u;

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(request.left, 3u);
    check_equal(request.right, 4u);
    check_equal(request.scale, 1u);
    check_equal(request.presence, (uint8_t)(1u << 0));

    response.sum = request.left + request.right * request.scale;
    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(state.published_sum, 7u);

    check_equal(data_bind_native_clear(
                    &options, &ADD_REQUEST_DATA, &request,
                    sizeof(request), &native_diagnostic),
                DATA_BIND_OK);
    native_diagnostic =
        (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_native_clear(
                    &options, &ADD_RESPONSE_DATA, &response,
                    sizeof(response), &native_diagnostic),
                DATA_BIND_OK);

    data_bind_binding_plan_free(plan);
  }

  it("binds multi-parameter IN and OUT native functions") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-v1", &scratch, rpc_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_fields));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {0};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    uint32_t left = 0u, right = 0u, scale = 0u, sum = 0u;
    void *params[] = {&left, &right, &scale, &sum};
    const size_t param_bytes[] = {
        sizeof(left), sizeof(right), sizeof(scale), sizeof(sum)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 4u;

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(left, 3u);
    check_equal(right, 4u);
    check_equal(scale, 1u);
    check_equal(sum, 0u);

    sum = left + right * scale;
    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(state.published_sum, 7u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("clears only initialized direct parameters once in reverse order") {
    DataBind *codec = create_rollback_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-rollback-order", &scratch, rpc_project);
    DataBindServiceNativeBinding native = rollback_native_binding();
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {0};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    RollbackProbeBuffer first = {0};
    RollbackProbeBuffer second = {0};
    RollbackProbeBuffer third = {0};
    RollbackProbeBuffer baseline = {0};
    void *params[] = {&first, &second, &third};
    const size_t param_bytes[] = {
        sizeof(first), sizeof(second), sizeof(third)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    size_t failed_init_restore_baseline;
    DataBindBindingCallLifetime lifetime = DATA_BIND_BINDING_CALL_LIFETIME_INIT;

    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Rollback", "Run", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    /*
     * Establish how many restore callbacks belong to the failing provider's
     * own failure-atomic init path. BindingPlan rollback must not add another
     * clear for that never-initialized parameter.
     */
    rollback_probe_reset();
    check_equal(data_bind_native_init(
                    &options, &ROLLBACK_THIRD_DATA, &baseline,
                    sizeof(baseline), &native_diagnostic),
                DATA_BIND_ERR_RUNTIME);
    failed_init_restore_baseline =
        ROLLBACK_LIFECYCLE_PROBE.restore_calls[2];
    check(failed_init_restore_baseline != 0u);

    rollback_probe_reset();
    native_diagnostic =
        (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    memset(&first, 0xa1, sizeof(first));
    memset(&second, 0xb2, sizeof(second));
    memset(&third, 0xc3, sizeof(third));

    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 3u;

    check_equal(data_bind_binding_plan_bind_call(
                    plan, &provider, &options, &frame, &lifetime, &diagnostic),
                DATA_BIND_ERR_RUNTIME);
    check_equal(diagnostic.function_param, "third");

    check_equal(ROLLBACK_LIFECYCLE_PROBE.init_calls[0], (size_t)1u);
    check_equal(ROLLBACK_LIFECYCLE_PROBE.init_calls[1], (size_t)1u);
    check_equal(ROLLBACK_LIFECYCLE_PROBE.init_calls[2], (size_t)1u);

    check_equal(ROLLBACK_LIFECYCLE_PROBE.restore_calls[0], (size_t)1u);
    check_equal(ROLLBACK_LIFECYCLE_PROBE.restore_calls[1], (size_t)1u);
    check_equal(
        ROLLBACK_LIFECYCLE_PROBE.restore_calls[2],
        failed_init_restore_baseline);

    check_equal(
        ROLLBACK_LIFECYCLE_PROBE.restore_order_count, (size_t)2u);
    check_equal(ROLLBACK_LIFECYCLE_PROBE.restore_order[0], 1u);
    check_equal(ROLLBACK_LIFECYCLE_PROBE.restore_order[1], 0u);

    check_true(rollback_buffer_is_zero(&first));
    check_true(rollback_buffer_is_zero(&second));
    check_true(rollback_buffer_is_zero(&third));

    check_false(data_bind_binding_call_is_live(&lifetime));
    check_equal(data_bind_binding_call_restore_zero(
                    &lifetime, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
    check_equal(ROLLBACK_LIFECYCLE_PROBE.restore_calls[0], (size_t)1u);
    check_equal(ROLLBACK_LIFECYCLE_PROBE.restore_calls[1], (size_t)1u);
    check_equal(ROLLBACK_LIFECYCLE_PROBE.restore_calls[2], failed_init_restore_baseline);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("rolls back native staging and presence after ingress decode failure") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &scratch, http_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {.fail_right_type = 1, .provide_scale = 1};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    AddRequest request = {
        .left = 91u, .right = 92u, .scale = 93u, .presence = 0xffu};
    AddResponse response = {.sum = 94u};
    void *params[] = {NULL, &response};
    const size_t param_bytes[] = {0u, sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 2u;

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(diagnostic.schema_field, "right");
    check_equal(request.left, 0u);
    check_equal(request.right, 0u);
    check_equal(request.scale, 0u);
    check_equal(request.presence, 0u);
    check_equal(response.sum, 0u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("validates root-request native staging and rolls back on semantic failure") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-validation", &scratch, http_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {
        .provide_scale = 1,
        .fail_right_validation = 1};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    AddRequest request = {
        .left = 91u, .right = 92u, .scale = 93u, .presence = 0xffu};
    AddResponse response = {.sum = 94u};
    void *params[] = {NULL, &response};
    const size_t param_bytes[] = {0u, sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    data_bind_free(codec);
    codec = NULL;

    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 2u;

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_ERR_VALIDATION);
    check_equal(diagnostic.schema_field, "right");
    check(strstr(diagnostic.message, "@Max") != NULL);
    check_equal(request.left, 0u);
    check_equal(request.right, 0u);
    check_equal(request.scale, 0u);
    check_equal(request.presence, 0u);
    check_equal(response.sum, 0u);

    data_bind_binding_plan_free(plan);
  }

  it("validates direct native parameters without runtime schema lookup") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-validation", &scratch, rpc_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_fields));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {
        .provide_scale = 1,
        .fail_right_validation = 1};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    uint32_t left = 0u, right = 0u, scale = 0u, sum = 0u;
    void *params[] = {&left, &right, &scale, &sum};
    const size_t param_bytes[] = {
        sizeof(left), sizeof(right), sizeof(scale), sizeof(sum)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    data_bind_free(codec);
    codec = NULL;

    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 4u;

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_ERR_VALIDATION);
    check_equal(diagnostic.schema_field, "right");
    check(strstr(diagnostic.message, "@Max") != NULL);
    check_equal(left, 0u);
    check_equal(right, 0u);
    check_equal(scale, 0u);
    check_equal(sum, 0u);

    data_bind_binding_plan_free(plan);
  }

  it("publishes BindingPlan egress through the format-neutral native encoder") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-v1", &scratch, rpc_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_fields));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    EncodeOutputProvider state = {
        .options = &options,
        .native_diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT};
    DataBindBindingProvider provider = encode_provider_for(&state);
    uint32_t left = 0u, right = 0u, scale = 0u, sum = 17u;
    void *params[] = {&left, &right, &scale, &sum};
    const size_t param_bytes[] = {
        sizeof(left), sizeof(right), sizeof(scale), sizeof(sum)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);

    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 4u;

    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(state.begin_calls, 1u);
    check_equal(state.write_calls, 1u);
    check_equal(state.commit_calls, 1u);
    check_equal(state.abort_calls, 0u);
    check_equal(state.writer_context.write_calls, 1u);
    check_true(state.writer_context.token.kind == CSERDE_UINT);
    check_equal(state.writer_context.token.value.uint, UINT64_C(17));
    check_equal(state.published_sum, 17u);
    check_true(state.writer.state == CSERDE_WRITER_READY);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("validates response before starting the output transaction") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-egress-validation", &scratch, rpc_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_fields));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {.published_sum = 77u};
    DataBindBindingProvider provider = provider_for(&state);
    uint32_t left = 0u, right = 0u, scale = 0u, sum = 101u;
    void *params[] = {&left, &right, &scale, &sum};
    const size_t param_bytes[] = {
        sizeof(left), sizeof(right), sizeof(scale), sizeof(sum)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 4u;

    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_ERR_VALIDATION);
    check_equal(diagnostic.schema_field, "sum");
    check(strstr(diagnostic.message, "@Max") != NULL);
    check_equal(state.begin_calls, (size_t)0u);
    check_equal(state.write_calls, (size_t)0u);
    check_equal(state.commit_calls, (size_t)0u);
    check_equal(state.abort_calls, (size_t)0u);
    check_equal(state.published_sum, 77u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("aborts transactional output publication on provider failure") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-v1", &scratch, rpc_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_fields));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {.fail_write = 1, .published_sum = 99u};
    DataBindBindingProvider provider = provider_for(&state);
    uint32_t left = 0u, right = 0u, scale = 0u, sum = 7u;
    void *params[] = {&left, &right, &scale, &sum};
    const size_t param_bytes[] = {
        sizeof(left), sizeof(right), sizeof(scale), sizeof(sum)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 4u;

    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_ERR_RUNTIME);
    check_equal(state.begin_calls, 1u);
    check_equal(state.write_calls, 1u);
    check_equal(state.commit_calls, 0u);
    check_equal(state.abort_calls, 1u);
    check_equal(state.published_sum, 99u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("preserves ABSENT NULL VALUE through dual native state overlays") {
    DataBind *codec = create_state_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-state", &scratch, rpc_project);
    DataBindServiceNativeBinding native = state_native_binding();
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    StateProvider state = {
        .required_state = DATA_BIND_VALUE_STATE_VALUE,
        .optional_state = DATA_BIND_VALUE_STATE_ABSENT,
        .nullable_state = DATA_BIND_VALUE_STATE_NULL,
        .defaulted_state = DATA_BIND_VALUE_STATE_ABSENT,
        .required_value = 11u,
        .optional_value = 22u,
        .nullable_value = 33u,
        .defaulted_value = 44u};
    DataBindBindingProvider provider = state_provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    StateRequest request = {
        .required_value = 91u,
        .optional_value = 92u,
        .nullable_value = 93u,
        .defaulted_value = 94u,
        .presence = 0xffu,
        .nulls = 0xffu};
    StateResponse response = {0};
    void *params[] = {NULL, &response};
    const size_t param_bytes[] = {0u, sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
    DataBindBindingPlanEntry entry = DATA_BIND_BINDING_PLAN_ENTRY_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "State", "Run", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    check(data_bind_binding_plan_ingress_at(plan, 2u, &entry));
    check_equal(entry.nullable, 1);
    check_equal(entry.has_null, 1);
    check_equal(entry.has_presence, 0);

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_ingress_at(plan, 3u, &entry));
    check_equal(entry.nullable, 1);
    check_equal(entry.has_null, 1);
    check_equal(entry.has_presence, 1);
    check_equal(entry.has_default, 1);

    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 2u;

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(request.required_value, 11u);
    check_equal(request.optional_value, 0u);
    check_equal(request.nullable_value, 0u);
    check_equal(request.defaulted_value, 7u);
    check_equal(request.presence, (uint8_t)(1u << 1));
    check_equal(request.nulls, (uint8_t)(1u << 0));

    check_equal(data_bind_native_clear(
                    &options, &STATE_REQUEST_DATA, &request,
                    sizeof(request), &native_diagnostic),
                DATA_BIND_OK);

    /* Explicit NULL is not replaced by the default. */
    state.nullable_state = DATA_BIND_VALUE_STATE_VALUE;
    state.nullable_value = 5u;
    state.defaulted_state = DATA_BIND_VALUE_STATE_NULL;
    request = (StateRequest){
        .required_value = 81u,
        .optional_value = 82u,
        .nullable_value = 83u,
        .defaulted_value = 84u,
        .presence = 0xffu,
        .nulls = 0xffu};
    native_diagnostic =
        (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(request.required_value, 11u);
    check_equal(request.optional_value, 0u);
    check_equal(request.nullable_value, 5u);
    check_equal(request.defaulted_value, 0u);
    check_equal(request.presence, (uint8_t)(1u << 1));
    check_equal(request.nulls, (uint8_t)(1u << 1));

    check_equal(data_bind_native_clear(
                    &options, &STATE_REQUEST_DATA, &request,
                    sizeof(request), &native_diagnostic),
                DATA_BIND_OK);

    /* A non-null field rejects explicit NULL and rolls back all state. */
    state.required_state = DATA_BIND_VALUE_STATE_NULL;
    request = (StateRequest){
        .required_value = 71u,
        .optional_value = 72u,
        .nullable_value = 73u,
        .defaulted_value = 74u,
        .presence = 0xffu,
        .nulls = 0xffu};
    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(diagnostic.schema_field, "required_value");
    check_equal(request.required_value, 0u);
    check_equal(request.optional_value, 0u);
    check_equal(request.nullable_value, 0u);
    check_equal(request.defaulted_value, 0u);
    check_equal(request.presence, 0u);
    check_equal(request.nulls, 0u);
    state.required_state = DATA_BIND_VALUE_STATE_VALUE;

    /* Required nullable NULL is published; optional nullable ABSENT is omitted. */
    response = (StateResponse){
        .nullable_result = 101u,
        .tri_result = 102u,
        .presence = 0u,
        .nulls = (uint8_t)(1u << 0)};
    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(state.write_calls, 2u);
    check_equal(state.null_calls, 1u);
    check_equal(state.absent_calls, 1u);
    check_equal(state.value_calls, 0u);
    check_equal(state.nullable_result_state, DATA_BIND_VALUE_STATE_NULL);
    check_equal(state.tri_result_state, DATA_BIND_VALUE_STATE_ABSENT);

    /* Required nullable VALUE plus optional nullable NULL are distinct. */
    response = (StateResponse){
        .nullable_result = 13u,
        .tri_result = 14u,
        .presence = (uint8_t)(1u << 0),
        .nulls = (uint8_t)(1u << 1)};
    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(state.write_calls, 2u);
    check_equal(state.value_calls, 1u);
    check_equal(state.null_calls, 1u);
    check_equal(state.absent_calls, 0u);
    check_equal(state.nullable_result_state, DATA_BIND_VALUE_STATE_VALUE);
    check_equal(state.nullable_result_value, 13u);
    check_equal(state.tri_result_state, DATA_BIND_VALUE_STATE_NULL);

    /* null=1 while presence=0 is not a canonical native state. */
    response = (StateResponse){
        .nullable_result = 15u,
        .tri_result = 16u,
        .presence = 0u,
        .nulls = (uint8_t)(1u << 1)};
    {
      size_t begin_before = state.begin_calls;
      size_t write_before = state.write_calls;
      size_t commit_before = state.commit_calls;
      size_t abort_before = state.abort_calls;
      check_equal(data_bind_binding_plan_write_outputs(
                      plan, &provider, &frame, &diagnostic),
                  DATA_BIND_ERR_SCHEMA);
      check_equal(diagnostic.schema_field, "tri_result");
      check_equal(state.begin_calls, begin_before);
      check_equal(state.write_calls, write_before);
      check_equal(state.commit_calls, commit_before);
      check_equal(state.abort_calls, abort_before);
    }

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("writes client requests without reapplying defaults and validates before begin") {
    DataBind *codec = create_state_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-client-request", &scratch, rpc_project);
    DataBindServiceNativeBinding native = state_native_binding();
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    StateProvider state = {0};
    DataBindBindingProvider provider = state_provider_for(&state);
    StateRequest request = {
        .required_value = 11u,
        .optional_value = 22u,
        .nullable_value = 33u,
        .defaulted_value = 44u,
        .presence = 0u,
        .nulls = (uint8_t)(1u << 0)};
    void *params[] = {NULL, NULL};
    const size_t param_bytes[] = {0u, 0u};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "State", "Run", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 2u;

    check_equal(data_bind_binding_plan_write_inputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(state.begin_calls, (size_t)1u);
    check_equal(state.write_calls, (size_t)4u);
    check_equal(state.commit_calls, (size_t)1u);
    check_equal(state.abort_calls, (size_t)0u);
    check_equal(state.written_required_state, DATA_BIND_VALUE_STATE_VALUE);
    check_equal(state.written_required_value, (uint32_t)11u);
    check_equal(state.written_optional_state, DATA_BIND_VALUE_STATE_ABSENT);
    check_equal(state.written_nullable_state, DATA_BIND_VALUE_STATE_NULL);
    check_equal(state.written_defaulted_state, DATA_BIND_VALUE_STATE_ABSENT);
    check_equal(request.defaulted_value, (uint32_t)44u);
    check_equal(request.presence, (uint8_t)0u);
    check_equal(request.nulls, (uint8_t)(1u << 0));

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("validates client requests before beginning provider output") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-client-request-validation", &scratch, rpc_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    unsigned char workspace[4096] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    EncodeOutputProvider state = {
        .options = &options,
        .native_diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT};
    DataBindBindingProvider provider = encode_provider_for(&state);
    AddRequest request = {
        .left = 0u,
        .right = 4u,
        .scale = 2u,
        .presence = (uint8_t)(1u << 0)};
    void *params[] = {NULL, NULL};
    const size_t param_bytes[] = {0u, 0u};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 2u;

    check_equal(data_bind_binding_plan_write_inputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_ERR_VALIDATION);
    check_equal(diagnostic.schema_field, "left");
    check_equal(state.begin_calls, (size_t)0u);
    check_equal(state.write_calls, (size_t)0u);
    check_equal(state.commit_calls, (size_t)0u);
    check_equal(state.abort_calls, (size_t)0u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("binds client success results with native state and rolls malformed input back") {
    DataBind *codec = create_state_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-client-result", &scratch, rpc_project);
    DataBindServiceNativeBinding native = state_native_binding();
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    unsigned char workspace[4096] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    StateProvider state = {
        .nullable_result_state = DATA_BIND_VALUE_STATE_VALUE,
        .tri_result_state = DATA_BIND_VALUE_STATE_NULL,
        .nullable_result_value = 13u};
    DataBindBindingProvider provider = state_provider_for(&state);
    StateResponse response = {0};
    void *params[] = {NULL, &response};
    const size_t param_bytes[] = {0u, sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
    DataBindBindingOutcome outcome = DATA_BIND_BINDING_OUTCOME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "State", "Run", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 2u;
    outcome.kind = DATA_BIND_BINDING_OUTCOME_SUCCESS;

    check_equal(data_bind_binding_plan_bind_outcome(
                    plan, &provider, &options, &frame,
                    &outcome, &diagnostic),
                DATA_BIND_OK);
    check_equal(response.nullable_result, (uint32_t)13u);
    check_equal(response.tri_result, (uint32_t)0u);
    check_equal(response.presence, (uint8_t)(1u << 0));
    check_equal(response.nulls, (uint8_t)(1u << 1));

    {
      DataBindNativeDiagnostic native_diagnostic =
          DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
      check_equal(data_bind_native_clear(
                      &options, &STATE_RESPONSE_DATA, &response,
                      sizeof(response), &native_diagnostic),
                  DATA_BIND_OK);
      response.presence = 0u;
      response.nulls = 0u;
    }

    state.fail_result_type = 1;
    state.tri_result_state = DATA_BIND_VALUE_STATE_ABSENT;
    check_equal(data_bind_binding_plan_bind_outcome(
                    plan, &provider, &options, &frame,
                    &outcome, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(response.nullable_result, (uint32_t)0u);
    check_equal(response.tri_result, (uint32_t)0u);
    check_equal(response.presence, (uint8_t)0u);
    check_equal(response.nulls, (uint8_t)0u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("binds a direct scalar client result without request staging") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-client-scalar", &scratch, rpc_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_fields));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    unsigned char workspace[4096] = {0};
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    TestProvider state = {.result_sum = 17u};
    DataBindBindingProvider provider = provider_for(&state);
    uint32_t sum = 0u;
    void *params[] = {NULL, NULL, NULL, &sum};
    const size_t param_bytes[] = {0u, 0u, 0u, sizeof(sum)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
    DataBindBindingOutcome outcome = DATA_BIND_BINDING_OUTCOME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 4u;
    outcome.kind = DATA_BIND_BINDING_OUTCOME_SUCCESS;

    check_equal(data_bind_binding_plan_bind_outcome(
                    plan, &provider, &options, &frame,
                    &outcome, &diagnostic),
                DATA_BIND_OK);
    check_equal(sum, (uint32_t)17u);

    sum = 0u;
    state.result_sum = 101u;
    check_equal(data_bind_binding_plan_bind_outcome(
                    plan, &provider, &options, &frame,
                    &outcome, &diagnostic),
                DATA_BIND_ERR_VALIDATION);
    check_equal(diagnostic.schema_field, "sum");
    check_equal(sum, (uint32_t)0u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("rejects malformed native state bindings") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &scratch, http_project);
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;

    const DataBindNativeStateBinding required_presence[] = {
        {sizeof(DataBindNativeStateBinding), "left",
         offsetof(AddRequest, presence), 1u}};
    DataBindNativeTypeBinding bad_required = ADD_REQUEST_NATIVE;
    DataBindServiceNativeBinding native;

    bad_required.presence = required_presence;
    bad_required.presence_count = 1u;
    native = (DataBindServiceNativeBinding)
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(calc_add_root), &bad_required, &ADD_RESPONSE_NATIVE);

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_equal(diagnostic.schema_field, "left");

    {
      const DataBindNativeStateBinding duplicate_presence[] = {
          {sizeof(DataBindNativeStateBinding), "scale",
           offsetof(AddRequest, presence), 0u},
          {sizeof(DataBindNativeStateBinding), "scale",
           offsetof(AddRequest, presence), 0u}};
      DataBindNativeTypeBinding bad_duplicate = ADD_REQUEST_NATIVE;

      bad_duplicate.presence = duplicate_presence;
      bad_duplicate.presence_count = 2u;
      diagnostic =
          (DataBindBindingPlanDiagnostic)DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
      native = (DataBindServiceNativeBinding)
          DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
              FunctionMeta(calc_add_root), &bad_duplicate,
              &ADD_RESPONSE_NATIVE);

      check_equal(data_bind_binding_plan_compile_service(
                      codec, "Calc", "Add", &http, &native,
                      &plan, &diagnostic),
                  DATA_BIND_ERR_SCHEMA);
      check_null(plan);
      check_equal(diagnostic.schema_field, "scale");
      check(strstr(diagnostic.message, "Duplicate") != NULL);
    }

    {
      const DataBindNativeStateBinding overlapping_presence[] = {
          {sizeof(DataBindNativeStateBinding), "scale",
           offsetof(AddRequest, left), 0u}};
      DataBindNativeTypeBinding bad_overlap = ADD_REQUEST_NATIVE;

      bad_overlap.presence = overlapping_presence;
      bad_overlap.presence_count = 1u;
      diagnostic =
          (DataBindBindingPlanDiagnostic)DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
      native = (DataBindServiceNativeBinding)
          DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
              FunctionMeta(calc_add_root), &bad_overlap,
              &ADD_RESPONSE_NATIVE);

      check_equal(data_bind_binding_plan_compile_service(
                      codec, "Calc", "Add", &http, &native,
                      &plan, &diagnostic),
                  DATA_BIND_ERR_SCHEMA);
      check_null(plan);
      check_equal(diagnostic.schema_field, "scale");
      check(strstr(diagnostic.message, "overlaps") != NULL);
    }

    data_bind_free(codec);
  }

  it("rejects ambiguous pointer-return response ownership") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &scratch, http_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_bad_pointer_return));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(plan);
    check(strstr(diagnostic.message, "ownership") != NULL);

    data_bind_free(codec);
  }
}

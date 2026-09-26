#include "data_bind_binding_plan.h"
#include "tinytest.h"

#include <cmeta/function.h>
#include <cstl/byte_buffer.h>
#include <salts_cmeta_data.h>
#include <tstr.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct OwnedRequest {
  uint32_t id;
} OwnedRequest;

typedef struct OwnedResponse {
  uint32_t value;
} OwnedResponse;

typedef struct OwnedTextError {
  tstr detail;
} OwnedTextError;

typedef struct OwnedBytesError {
  stl_byte_buffer payload;
} OwnedBytesError;

typedef struct OwnedErrorEnvelope {
  uint32_t kind;
  union {
    OwnedTextError text;
    OwnedBytesError bytes;
  } payload;
} OwnedErrorEnvelope;

static const cmeta_type_identity REQUEST_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.owned.Request");
static const cmeta_type_desc REQUEST_TYPE = {
    "OwnedRequest", sizeof(OwnedRequest), _Alignof(OwnedRequest),
    CMETA_T_OBJECT, NULL, NULL, &REQUEST_ID};
static const cmeta_field_desc REQUEST_LAYOUT_FIELDS[] = {
    {"id", "uint32_t", offsetof(OwnedRequest, id), sizeof(uint32_t),
     _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL}};
static const cmeta_struct_desc REQUEST_LAYOUT = {
    "OwnedRequest", sizeof(OwnedRequest), _Alignof(OwnedRequest),
    REQUEST_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc REQUEST_FIELDS[] = {
    {"test.owned.Request.id", "id", offsetof(OwnedRequest, id),
     &salts_uint32_cmeta_data}};
static const cmeta_data_struct_shape REQUEST_SHAPE = {
    &REQUEST_LAYOUT, REQUEST_FIELDS, 1u};
static const cmeta_data_desc REQUEST_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.owned.Request.data",
    .display_name = "OwnedRequest",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &REQUEST_TYPE,
    .shape = &REQUEST_SHAPE};

static const cmeta_type_identity RESPONSE_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.owned.Response");
static const cmeta_type_desc RESPONSE_TYPE = {
    "OwnedResponse", sizeof(OwnedResponse), _Alignof(OwnedResponse),
    CMETA_T_OBJECT, NULL, NULL, &RESPONSE_ID};
static const cmeta_field_desc RESPONSE_LAYOUT_FIELDS[] = {
    {"value", "uint32_t", offsetof(OwnedResponse, value), sizeof(uint32_t),
     _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL}};
static const cmeta_struct_desc RESPONSE_LAYOUT = {
    "OwnedResponse", sizeof(OwnedResponse), _Alignof(OwnedResponse),
    RESPONSE_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc RESPONSE_FIELDS[] = {
    {"test.owned.Response.value", "value", offsetof(OwnedResponse, value),
     &salts_uint32_cmeta_data}};
static const cmeta_data_struct_shape RESPONSE_SHAPE = {
    &RESPONSE_LAYOUT, RESPONSE_FIELDS, 1u};
static const cmeta_data_desc RESPONSE_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.owned.Response.data",
    .display_name = "OwnedResponse",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &RESPONSE_TYPE,
    .shape = &RESPONSE_SHAPE};

static const cmeta_type_identity TEXT_ERROR_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.owned.TextError");
static const cmeta_type_desc TEXT_ERROR_TYPE = {
    "OwnedTextError", sizeof(OwnedTextError), _Alignof(OwnedTextError),
    CMETA_T_OBJECT, NULL, NULL, &TEXT_ERROR_ID};
static const cmeta_field_desc TEXT_ERROR_LAYOUT_FIELDS[] = {
    {"detail", "tstr", offsetof(OwnedTextError, detail), sizeof(tstr),
     _Alignof(tstr), &salts_tstr_cmeta_type, NULL}};
static const cmeta_struct_desc TEXT_ERROR_LAYOUT = {
    "OwnedTextError", sizeof(OwnedTextError), _Alignof(OwnedTextError),
    TEXT_ERROR_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc TEXT_ERROR_FIELDS[] = {
    {"test.owned.TextError.detail", "detail", offsetof(OwnedTextError, detail),
     &salts_tstr_cmeta_data}};
static const cmeta_data_struct_shape TEXT_ERROR_SHAPE = {
    &TEXT_ERROR_LAYOUT, TEXT_ERROR_FIELDS, 1u};
static const cmeta_data_desc TEXT_ERROR_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.owned.TextError.data",
    .display_name = "OwnedTextError",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &TEXT_ERROR_TYPE,
    .shape = &TEXT_ERROR_SHAPE};

static const cmeta_type_identity BYTES_ERROR_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.owned.BytesError");
static const cmeta_type_desc BYTES_ERROR_TYPE = {
    "OwnedBytesError", sizeof(OwnedBytesError), _Alignof(OwnedBytesError),
    CMETA_T_OBJECT, NULL, NULL, &BYTES_ERROR_ID};
static const cmeta_field_desc BYTES_ERROR_LAYOUT_FIELDS[] = {
    {"payload", "stl_byte_buffer", offsetof(OwnedBytesError, payload),
     sizeof(stl_byte_buffer), _Alignof(stl_byte_buffer),
     &stl_byte_buffer_cmeta_type, NULL}};
static const cmeta_struct_desc BYTES_ERROR_LAYOUT = {
    "OwnedBytesError", sizeof(OwnedBytesError), _Alignof(OwnedBytesError),
    BYTES_ERROR_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc BYTES_ERROR_FIELDS[] = {
    {"test.owned.BytesError.payload", "payload",
     offsetof(OwnedBytesError, payload), &stl_byte_buffer_cmeta_data}};
static const cmeta_data_struct_shape BYTES_ERROR_SHAPE = {
    &BYTES_ERROR_LAYOUT, BYTES_ERROR_FIELDS, 1u};
static const cmeta_data_desc BYTES_ERROR_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.owned.BytesError.data",
    .display_name = "OwnedBytesError",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &BYTES_ERROR_TYPE,
    .shape = &BYTES_ERROR_SHAPE};

static const cmeta_type_desc REQUEST_PTR_TYPE = {
    "const OwnedRequest *", sizeof(OwnedRequest *), _Alignof(OwnedRequest *),
    CMETA_T_POINTER, &REQUEST_TYPE, NULL, NULL};
static const cmeta_type_desc RESPONSE_PTR_TYPE = {
    "OwnedResponse *", sizeof(OwnedResponse *), _Alignof(OwnedResponse *),
    CMETA_T_POINTER, &RESPONSE_TYPE, NULL, NULL};
static const cmeta_type_identity ERROR_ENVELOPE_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.owned.ErrorEnvelope");
static const cmeta_type_desc ERROR_ENVELOPE_TYPE = {
    "OwnedErrorEnvelope", sizeof(OwnedErrorEnvelope), _Alignof(OwnedErrorEnvelope),
    CMETA_T_OBJECT, NULL, NULL, &ERROR_ENVELOPE_ID};
static const cmeta_type_desc ERROR_ENVELOPE_PTR_TYPE = {
    "OwnedErrorEnvelope *", sizeof(OwnedErrorEnvelope *),
    _Alignof(OwnedErrorEnvelope *), CMETA_T_POINTER,
    &ERROR_ENVELOPE_TYPE, NULL, NULL};

FunctionDeclAs(
    value, int, &cmeta_type_int, owned_call,
    (const OwnedRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &REQUEST_PTR_TYPE),
    (OwnedResponse *, response,
     CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, &RESPONSE_PTR_TYPE),
    (OwnedErrorEnvelope *, error,
     CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, &ERROR_ENVELOPE_PTR_TYPE));

static DataBindStatus resolve_text(
    const cmeta_data_desc **out, DataBindError *error) {
  (void)error;
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *out = &TEXT_ERROR_DATA;
  return DATA_BIND_OK;
}

static DataBindStatus resolve_bytes(
    const cmeta_data_desc **out, DataBindError *error) {
  (void)error;
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *out = &BYTES_ERROR_DATA;
  return DATA_BIND_OK;
}

static const DataBindNativeTypeBinding REQUEST_NATIVE =
    DATA_BIND_NATIVE_TYPE_BINDING_INIT("Request", &REQUEST_DATA);
static const DataBindNativeTypeBinding RESPONSE_NATIVE =
    DATA_BIND_NATIVE_TYPE_BINDING_INIT("Response", &RESPONSE_DATA);

static const DataBindNativeErrorBinding ERROR_BINDINGS[] = {
    {sizeof(DataBindNativeErrorBinding), "TextError", 1u, resolve_text,
     offsetof(OwnedErrorEnvelope, payload.text)},
    {sizeof(DataBindNativeErrorBinding), "BytesError", 2u, resolve_bytes,
     offsetof(OwnedErrorEnvelope, payload.bytes)}};

static DataBindStatus project_field(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  (void)context;
  (void)operation;
  (void)error;
  if (field == NULL || out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  out->binding_class =
      direction == DATA_BIND_BINDING_INGRESS
          ? DATA_BIND_BINDING_VALUE
          : DATA_BIND_BINDING_RESULT;
  out->space =
      direction == DATA_BIND_BINDING_INGRESS ? "request" : "response";
  out->name = field->name;
  return DATA_BIND_OK;
}

typedef struct OutputProbe {
  size_t begin_calls;
  size_t write_calls;
  size_t commit_calls;
  size_t abort_calls;
} OutputProbe;

static DataBindStatus begin_output(void *context, DataBindError *error) {
  OutputProbe *probe = (OutputProbe *)context;
  (void)error;
  ++probe->begin_calls;
  return DATA_BIND_OK;
}

static DataBindStatus write_output(
    void *context, const DataBindBindingPlanEntry *entry,
    DataBindBindingValueState state, const void *value, size_t value_bytes,
    DataBindError *error) {
  OutputProbe *probe = (OutputProbe *)context;
  (void)entry;
  (void)state;
  (void)value;
  (void)value_bytes;
  (void)error;
  ++probe->write_calls;
  return DATA_BIND_OK;
}

static DataBindStatus commit_output(void *context, DataBindError *error) {
  OutputProbe *probe = (OutputProbe *)context;
  (void)error;
  ++probe->commit_calls;
  return DATA_BIND_OK;
}

static void abort_output(void *context) {
  OutputProbe *probe = (OutputProbe *)context;
  ++probe->abort_calls;
}

static DataBind *create_codec(void) {
  static const char schema[] =
      "message Request { uint32 id; }"
      "message Response { uint32 value; }"
      "message TextError { @Size(max = 4) string detail; }"
      "message BytesError { @Size(max = 3) bytes payload; }"
      "service Store { Read: Request -> Response throws TextError, BytesError; }";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  return data_bind_create_from_text(
             schema, sizeof(schema) - 1u, &codec, &error) == DATA_BIND_OK
             ? codec
             : NULL;
}

static DataBindBindingPlan *compile_plan(DataBind *codec) {
  DataBindServiceNativeBinding native =
      DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
          FunctionMeta(owned_call), &REQUEST_NATIVE, &RESPONSE_NATIVE);
  DataBindBindingProjection projection =
      DATA_BIND_BINDING_PROJECTION_INIT;
  projection.id = "test-owned";
  projection.project_field = project_field;
  DataBindBindingPlanDiagnostic diagnostic =
      DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
  DataBindBindingPlan *plan = NULL;

  native.errors = ERROR_BINDINGS;
  native.error_count = 2u;
  native.error_param_index = 2u;
  native.error_envelope_bytes = sizeof(OwnedErrorEnvelope);
  native.error_kind_offset = offsetof(OwnedErrorEnvelope, kind);
  native.error_kind_bytes = sizeof(uint32_t);

  check_equal(data_bind_binding_plan_compile_service(
                  codec, "Store", "Read", &projection, &native,
                  &plan, &diagnostic),
              DATA_BIND_OK);
  return plan;
}

static void expect_pretransaction_validation(
    DataBindBindingPlan *plan, OwnedErrorEnvelope *error,
    const char *expected_path) {
  OwnedRequest request = {0};
  OwnedResponse response = {0};
  OutputProbe probe = {0};
  DataBindBindingProvider provider = DATA_BIND_BINDING_PROVIDER_INIT;
  DataBindBindingPlanDiagnostic diagnostic =
      DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
  DataBindBindingOutcome outcome = DATA_BIND_BINDING_OUTCOME_INIT;
  void *params[] = {&request, &response, error};
  const size_t param_bytes[] = {
      sizeof(request), sizeof(response), sizeof(*error)};
  DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

  provider.context = &probe;
  provider.begin_output = begin_output;
  provider.write_output = write_output;
  provider.commit_output = commit_output;
  provider.abort_output = abort_output;

  frame.request = &request;
  frame.request_bytes = sizeof(request);
  frame.params = params;
  frame.param_bytes = param_bytes;
  frame.param_count = 3u;

  check_equal(data_bind_binding_plan_write_outcome(
                  plan, &provider, &frame, 0, &outcome, &diagnostic),
              DATA_BIND_ERR_VALIDATION);
  check_contains(diagnostic.schema_field, expected_path);
  check_equal(probe.begin_calls, (size_t)0u);
  check_equal(probe.write_calls, (size_t)0u);
  check_equal(probe.commit_calls, (size_t)0u);
  check_equal(probe.abort_calls, (size_t)0u);
}

spec("BindingPlan validates owned typed errors before publication") {
  it("rejects oversized string and bytes payloads before provider side effects") {
    DataBind *codec = create_codec();
    DataBindBindingPlan *plan = NULL;
    OwnedErrorEnvelope error = {0};

    check_not_null(codec);
    if (codec == NULL) return;
    plan = compile_plan(codec);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    error.kind = 1u;
    check_equal(cmeta_data_value_init_zero(
                    &TEXT_ERROR_DATA, &error.payload.text),
                CMETA_OK);
    error.payload.text.detail = tstr_dup("too-long");
    check_not_null(error.payload.text.detail);
    expect_pretransaction_validation(plan, &error, "TextError.detail");
    check_equal(cmeta_data_value_restore_zero(
                    &TEXT_ERROR_DATA, &error.payload.text),
                CMETA_OK);

    memset(&error, 0, sizeof(error));
    error.kind = 2u;
    check_equal(cmeta_data_value_init_zero(
                    &BYTES_ERROR_DATA, &error.payload.bytes),
                CMETA_OK);
    check_equal(stl_byte_buffer_resize(
                    &error.payload.bytes.payload, 4u),
                STL_OK);
    expect_pretransaction_validation(plan, &error, "BytesError.payload");
    check_equal(cmeta_data_value_restore_zero(
                    &BYTES_ERROR_DATA, &error.payload.bytes),
                CMETA_OK);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }
}

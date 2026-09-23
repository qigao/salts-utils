#include "data_bind_binding_plan.h"
#include "tinytest.h"

#include <salts_cmeta_fixed_width.h>

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

static const cmeta_type_desc ADD_REQUEST_TYPE = {
    "AddRequest", sizeof(AddRequest), _Alignof(AddRequest),
    CMETA_T_OBJECT, NULL, NULL, &ADD_REQUEST_ID};
static const cmeta_type_desc ADD_RESPONSE_TYPE = {
    "AddResponse", sizeof(AddResponse), _Alignof(AddResponse),
    CMETA_T_OBJECT, NULL, NULL, &ADD_RESPONSE_ID};

static const cmeta_type_desc ADD_REQUEST_PTR_TYPE = {
    "const AddRequest *", sizeof(AddRequest *), _Alignof(AddRequest *),
    CMETA_T_POINTER, &ADD_REQUEST_TYPE, NULL, NULL};
static const cmeta_type_desc ADD_RESPONSE_PTR_TYPE = {
    "AddResponse *", sizeof(AddResponse *), _Alignof(AddResponse *),
    CMETA_T_POINTER, &ADD_RESPONSE_TYPE, NULL, NULL};
static const cmeta_type_desc UINT32_PTR_TYPE = {
    "uint32_t *", sizeof(uint32_t *), _Alignof(uint32_t *),
    CMETA_T_POINTER, &salts_uint32_cmeta_type, NULL, NULL};

static const cmeta_field_desc ADD_REQUEST_LAYOUT_FIELDS[] = {
    {"left", "uint32_t", offsetof(AddRequest, left), sizeof(uint32_t),
     _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL},
    {"right", "uint32_t", offsetof(AddRequest, right), sizeof(uint32_t),
     _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL},
    {"scale", "uint32_t", offsetof(AddRequest, scale), sizeof(uint32_t),
     _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL}};
static const cmeta_struct_desc ADD_REQUEST_LAYOUT = {
    "AddRequest", sizeof(AddRequest), _Alignof(AddRequest),
    ADD_REQUEST_LAYOUT_FIELDS, 3u};
static const cmeta_data_field_desc ADD_REQUEST_FIELDS[] = {
    {"test.calc.AddRequest.left", "left", offsetof(AddRequest, left),
     &salts_uint32_cmeta_data},
    {"test.calc.AddRequest.right", "right", offsetof(AddRequest, right),
     &salts_uint32_cmeta_data},
    {"test.calc.AddRequest.scale", "scale", offsetof(AddRequest, scale),
     &salts_uint32_cmeta_data}};
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
     _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL}};
static const cmeta_struct_desc ADD_RESPONSE_LAYOUT = {
    "AddResponse", sizeof(AddResponse), _Alignof(AddResponse),
    ADD_RESPONSE_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc ADD_RESPONSE_FIELDS[] = {
    {"test.calc.AddResponse.sum", "sum", offsetof(AddResponse, sum),
     &salts_uint32_cmeta_data}};
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

static const DataBindNativePresenceBinding ADD_REQUEST_PRESENCE[] = {
    {sizeof(DataBindNativePresenceBinding), "scale",
     offsetof(AddRequest, presence), 0u}};

static const DataBindNativeTypeBinding ADD_REQUEST_NATIVE = {
    sizeof(DataBindNativeTypeBinding),
    DATA_BIND_BINDING_PLAN_ABI_VERSION,
    "AddRequest",
    &ADD_REQUEST_DATA,
    ADD_REQUEST_PRESENCE,
    1u};

static const DataBindNativeTypeBinding ADD_RESPONSE_NATIVE = {
    sizeof(DataBindNativeTypeBinding),
    DATA_BIND_BINDING_PLAN_ABI_VERSION,
    "AddResponse",
    &ADD_RESPONSE_DATA,
    NULL,
    0u};

FunctionDeclAs(
    value, void, &cmeta_type_void, calc_add_root,
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE),
    (AddResponse *, response, CMETA_PARAM_OUT, &ADD_RESPONSE_PTR_TYPE));

FunctionDeclAs(
    value, void, &cmeta_type_void, calc_add_fields,
    (uint32_t, left, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, right, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, scale, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t *, sum, CMETA_PARAM_OUT, &UINT32_PTR_TYPE));

FunctionDeclAs(
    value, void, &cmeta_type_void, calc_bad_type,
    (int32_t, left, CMETA_PARAM_IN, &salts_int32_cmeta_type),
    (uint32_t, right, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, scale, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t *, sum, CMETA_PARAM_OUT, &UINT32_PTR_TYPE));

FunctionDeclAs(
    value, void, &cmeta_type_void, calc_bad_direction,
    (uint32_t, left, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, right, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, scale, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t *, sum, CMETA_PARAM_IN, &UINT32_PTR_TYPE));

FunctionDeclAs(
    value, AddResponse *, &ADD_RESPONSE_PTR_TYPE, calc_bad_pointer_return,
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE));

static DataBind *create_codec(void) {
  static const char schema[] =
      "message AddRequest {"
      " uint32 left;"
      " uint32 right;"
      " optional uint32 scale default 1;"
      "}"
      "message AddResponse { uint32 sum; }"
      "message CalcError { string detail; }"
      "service Calc {"
      " Add: AddRequest -> AddResponse throws CalcError;"
      "}";
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

typedef struct ProjectionContext {
  char space[32];
  char name[64];
} ProjectionContext;

static DataBindStatus http_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  ProjectionContext *state = (ProjectionContext *)context;
  (void)operation;
  (void)error;
  if (state == NULL || field == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (direction == DATA_BIND_BINDING_EGRESS) {
    out->binding_class = DATA_BIND_BINDING_RESULT;
    snprintf(state->space, sizeof(state->space), "http.result");
    snprintf(state->name, sizeof(state->name), "%s", field->name);
  } else if (strcmp(field->name, "right") == 0) {
    out->binding_class = DATA_BIND_BINDING_METADATA;
    snprintf(state->space, sizeof(state->space), "http.header");
    snprintf(state->name, sizeof(state->name), "X-Right");
  } else {
    out->binding_class = DATA_BIND_BINDING_VALUE;
    snprintf(state->space, sizeof(state->space), "http.query");
    snprintf(state->name, sizeof(state->name), "%s", field->name);
  }
  out->space = state->space;
  out->name = state->name;
  return DATA_BIND_OK;
}

static DataBindStatus rpc_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  ProjectionContext *state = (ProjectionContext *)context;
  (void)operation;
  (void)error;
  if (state == NULL || field == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  out->binding_class =
      direction == DATA_BIND_BINDING_INGRESS
          ? DATA_BIND_BINDING_VALUE
          : DATA_BIND_BINDING_RESULT;
  snprintf(state->space, sizeof(state->space), "%s",
           direction == DATA_BIND_BINDING_INGRESS
               ? "rpc.param" : "rpc.result");
  snprintf(state->name, sizeof(state->name), "%s", field->name);
  out->space = state->space;
  out->name = state->name;
  return DATA_BIND_OK;
}

static DataBindStatus mqtt_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  ProjectionContext *state = (ProjectionContext *)context;
  (void)operation;
  (void)error;
  if (state == NULL || field == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (direction == DATA_BIND_BINDING_EGRESS) {
    out->binding_class = DATA_BIND_BINDING_RESULT;
    snprintf(state->space, sizeof(state->space), "mqtt.result");
  } else if (strcmp(field->name, "left") == 0) {
    out->binding_class = DATA_BIND_BINDING_VALUE;
    snprintf(state->space, sizeof(state->space), "mqtt.topic");
  } else if (strcmp(field->name, "right") == 0) {
    out->binding_class = DATA_BIND_BINDING_PAYLOAD;
    snprintf(state->space, sizeof(state->space), "mqtt.payload");
  } else {
    out->binding_class = DATA_BIND_BINDING_METADATA;
    snprintf(state->space, sizeof(state->space), "mqtt.property");
  }
  snprintf(state->name, sizeof(state->name), "%s", field->name);
  out->space = state->space;
  out->name = state->name;
  return DATA_BIND_OK;
}

static DataBindBindingProjection projection(
    const char *id, ProjectionContext *context,
    DataBindBindingProjectFieldFn fn) {
  DataBindBindingProjection result = DATA_BIND_BINDING_PROJECTION_INIT;
  result.id = id;
  result.context = context;
  result.project_field = fn;
  return result;
}

typedef struct OneTokenReader {
  cserde_token token;
  int emitted;
} OneTokenReader;

static cserde_status one_token_next(void *context, cserde_token *out) {
  OneTokenReader *reader = (OneTokenReader *)context;
  if (reader == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (reader->emitted) return CSERDE_DONE;
  *out = reader->token;
  reader->emitted = 1;
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
  int fail_write;
  size_t begin_calls;
  size_t write_calls;
  size_t commit_calls;
  size_t abort_calls;
  uint32_t staged_sum;
  uint32_t published_sum;
} TestProvider;

static DataBindStatus provider_open(
    void *context, const DataBindBindingPlanEntry *entry,
    cserde_reader *reader, int *present, DataBindError *error) {
  TestProvider *provider = (TestProvider *)context;
  uint64_t value;
  const char *name;
  (void)error;

  if (provider == NULL || entry == NULL || reader == NULL || present == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  name = entry->address.name;
  *present = 1;
  if (name != NULL && strcmp(name, "left") == 0) {
    value = 3u;
  } else if (name != NULL &&
             (strcmp(name, "right") == 0 ||
              strcmp(name, "X-Right") == 0)) {
    if (provider->fail_right_type) {
      static const unsigned char invalid[] = "bad";
      provider->reader.token =
          (cserde_token){.kind = CSERDE_STRING,
                         .value.slice = {invalid, 3u, CSERDE_VIEW_STABLE}};
      provider->reader.emitted = 0;
      return cserde_reader_init(reader, &ONE_TOKEN_OPS,
                                &provider->reader) == CSERDE_OK
                 ? DATA_BIND_OK
                 : DATA_BIND_ERR_RUNTIME;
    }
    value = 4u;
  } else if (name != NULL && strcmp(name, "scale") == 0) {
    if (!provider->provide_scale) {
      *present = 0;
      return DATA_BIND_OK;
    }
    value = 2u;
  } else {
    return DATA_BIND_ERR_TYPE_NOT_FOUND;
  }

  provider->reader.token =
      (cserde_token){.kind = CSERDE_UINT, .value.uint = value};
  provider->reader.emitted = 0;
  return cserde_reader_init(
             reader, &ONE_TOKEN_OPS, &provider->reader) == CSERDE_OK
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
    const void *value, size_t value_bytes, DataBindError *error) {
  TestProvider *provider = (TestProvider *)context;
  (void)error;
  ++provider->write_calls;
  if (provider->fail_write) return DATA_BIND_ERR_RUNTIME;
  if (entry == NULL ||
      entry->address.binding_class != DATA_BIND_BINDING_RESULT ||
      entry->schema_field == NULL ||
      strcmp(entry->schema_field, "sum") != 0 ||
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
  DataBindBindingProvider result = DATA_BIND_BINDING_PROVIDER_INIT;
  result.context = state;
  result.open_input = provider_open;
  result.begin_output = provider_begin;
  result.write_output = provider_write;
  result.commit_output = provider_commit;
  result.abort_output = provider_abort;
  return result;
}

static DataBindNativeOptions native_options(
    unsigned char *workspace, size_t workspace_bytes) {
  DataBindNativeOptions result = DATA_BIND_NATIVE_OPTIONS_INIT;
  result.workspace = workspace;
  result.workspace_bytes = workspace_bytes;
  result.max_depth = 16u;
  result.max_items = 64u;
  result.max_owned_bytes = 1024u;
  return result;
}

spec("DataBind canonical Service BindingPlan") {
  it("compiles one service through HTTP RPC and MQTT projections") {
    DataBind *codec = create_codec();
    ProjectionContext http_ctx = {{0}, {0}};
    ProjectionContext rpc_ctx = {{0}, {0}};
    ProjectionContext mqtt_ctx = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &http_ctx, http_project);
    DataBindBindingProjection rpc =
        projection("rpc-v1", &rpc_ctx, rpc_project);
    DataBindBindingProjection mqtt =
        projection("mqtt-v1", &mqtt_ctx, mqtt_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *http_plan = NULL;
    DataBindBindingPlan *rpc_plan = NULL;
    DataBindBindingPlan *mqtt_plan = NULL;
    DataBindBindingPlanEntry entry = DATA_BIND_BINDING_PLAN_ENTRY_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http,
                    &native, &http_plan, &diagnostic),
                DATA_BIND_OK);
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &rpc,
                    &native, &rpc_plan, &diagnostic),
                DATA_BIND_OK);
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &mqtt,
                    &native, &mqtt_plan, &diagnostic),
                DATA_BIND_OK);

    check_equal(data_bind_binding_plan_operation_id(http_plan), "Calc.Add");
    check_equal(data_bind_binding_plan_projection_id(http_plan), "http-v1");
    check_equal(data_bind_binding_plan_error_count(http_plan), 1u);
    check_equal(data_bind_binding_plan_error_at(http_plan, 0u), "CalcError");

    check(data_bind_binding_plan_ingress_at(http_plan, 1u, &entry) == 1);
    check_true(entry.address.binding_class == DATA_BIND_BINDING_METADATA);
    check_equal(entry.address.space, "http.header");
    check_equal(entry.address.name, "X-Right");

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_ingress_at(mqtt_plan, 1u, &entry) == 1);
    check_true(entry.address.binding_class == DATA_BIND_BINDING_PAYLOAD);
    check_equal(entry.address.space, "mqtt.payload");

    data_bind_binding_plan_free(mqtt_plan);
    data_bind_binding_plan_free(rpc_plan);
    data_bind_binding_plan_free(http_plan);
    data_bind_free(codec);
  }

  it("executes a root request and OUT response after the codec is released") {
    DataBind *codec = create_codec();
    ProjectionContext ctx = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &ctx, http_project);
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
    void *params[] = {&request, &response};
    const size_t param_bytes[] = {sizeof(request), sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
    DataBindNativeDiagnostic native_diag = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http,
                    &native, &plan, &diagnostic),
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
    check_equal(request.presence, 0u);

    response.sum = request.left + request.right * request.scale;
    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(state.published_sum, 7u);

    check_equal(data_bind_native_clear(
                    &options, &ADD_REQUEST_DATA, &request,
                    sizeof(request), &native_diag),
                DATA_BIND_OK);
    request.presence = 0u;
    native_diag = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_native_clear(
                    &options, &ADD_RESPONSE_DATA, &response,
                    sizeof(response), &native_diag),
                DATA_BIND_OK);

    data_bind_binding_plan_free(plan);
  }

  it("supports field-mapped multi-parameter IN plus OUT binding") {
    DataBind *codec = create_codec();
    ProjectionContext ctx = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-v1", &ctx, rpc_project);
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
                    codec, "Calc", "Add", &rpc,
                    &native, &plan, &diagnostic),
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

  it("rolls back initialized staging and request presence on decode failure") {
    DataBind *codec = create_codec();
    ProjectionContext ctx = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &ctx, http_project);
    DataBindServiceNativeBinding native =
        native_binding(FunctionMeta(calc_add_root));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {.provide_scale = 1, .fail_right_type = 1};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    AddRequest request = {
        .left = 91u, .right = 92u, .scale = 93u, .presence = 0xffu};
    AddResponse response = {.sum = 94u};
    void *params[] = {&request, &response};
    const size_t param_bytes[] = {sizeof(request), sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http,
                    &native, &plan, &diagnostic),
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

  it("aborts output publication transaction on provider failure") {
    DataBind *codec = create_codec();
    ProjectionContext ctx = {{0}, {0}};
    DataBindBindingProjection rpc =
        projection("rpc-v1", &ctx, rpc_project);
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
                    codec, "Calc", "Add", &rpc,
                    &native, &plan, &diagnostic),
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

  it("rejects type direction presence and pointer-return ownership mismatches") {
    DataBind *codec = create_codec();
    ProjectionContext ctx = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &ctx, http_project);
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    DataBindServiceNativeBinding bad_type =
        native_binding(FunctionMeta(calc_bad_type));
    DataBindServiceNativeBinding bad_direction =
        native_binding(FunctionMeta(calc_bad_direction));
    DataBindNativeTypeBinding missing_presence = ADD_REQUEST_NATIVE;
    DataBindServiceNativeBinding bad_presence =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(calc_add_root),
            &missing_presence, &ADD_RESPONSE_NATIVE);
    DataBindServiceNativeBinding bad_return =
        native_binding(FunctionMeta(calc_bad_pointer_return));

    missing_presence.presence = NULL;
    missing_presence.presence_count = 0u;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http,
                    &bad_type, &plan, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(plan);
    check_equal(diagnostic.schema_field, "left");

    diagnostic =
        (DataBindBindingPlanDiagnostic)DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http,
                    &bad_direction, &plan, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(plan);
    check_equal(diagnostic.function_param, "sum");

    diagnostic =
        (DataBindBindingPlanDiagnostic)DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http,
                    &bad_presence, &plan, &diagnostic),
                DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_equal(diagnostic.schema_field, "scale");

    diagnostic =
        (DataBindBindingPlanDiagnostic)DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http,
                    &bad_return, &plan, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(plan);

    data_bind_free(codec);
  }
}

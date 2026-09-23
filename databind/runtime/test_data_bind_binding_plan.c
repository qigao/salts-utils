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
    value, int, &cmeta_type_int, calc_add_root,
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE),
    (AddResponse *, response, CMETA_PARAM_OUT, &ADD_RESPONSE_PTR_TYPE));

FunctionDeclAs(
    value, int, &cmeta_type_int, calc_add_fields,
    (uint32_t, left, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, right, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, scale, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t *, sum, CMETA_PARAM_OUT, &UINT32_PTR_TYPE));

FunctionDeclAs(
    value, AddResponse *, &ADD_RESPONSE_PTR_TYPE, calc_bad_pointer_return,
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE));

static DataBind *create_codec(void) {
  static const char schema[] =
      "message AddRequest {"
      " [query] uint32 left;"
      " [header(\"X-Right\")] uint32 right;"
      " optional [query] uint32 scale default 1;"
      "}"
      "message AddResponse { uint32 sum; }"
      "message CalcError { string detail; }"
      "service Calc {"
      " [GET(\"/add\"), rpc]"
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
  } else if (field->binding_kind != NULL &&
             strcmp(field->binding_kind, "header") == 0) {
    out->binding_class = DATA_BIND_BINDING_METADATA;
    snprintf(scratch->space, sizeof(scratch->space), "http.header");
    snprintf(scratch->name, sizeof(scratch->name), "%s",
             field->binding_name != NULL ? field->binding_name : field->name);
  } else if (field->binding_kind != NULL &&
             strcmp(field->binding_kind, "body") == 0) {
    out->binding_class = DATA_BIND_BINDING_PAYLOAD;
    snprintf(scratch->space, sizeof(scratch->space), "http.body");
    snprintf(scratch->name, sizeof(scratch->name), "%s", field->name);
  } else {
    out->binding_class = DATA_BIND_BINDING_VALUE;
    snprintf(scratch->space, sizeof(scratch->space), "http.query");
    snprintf(scratch->name, sizeof(scratch->name), "%s",
             field->binding_name != NULL ? field->binding_name : field->name);
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
  int fail_write;
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

static DataBindStatus provider_open(
    void *context, const DataBindBindingPlanEntry *entry,
    cserde_reader *reader, int *present, DataBindError *error) {
  TestProvider *provider = (TestProvider *)context;
  const char *name;
  uint64_t value;
  (void)error;

  if (provider == NULL || entry == NULL || reader == NULL || present == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (entry->address.binding_class != DATA_BIND_BINDING_VALUE &&
      entry->address.binding_class != DATA_BIND_BINDING_METADATA &&
      entry->address.binding_class != DATA_BIND_BINDING_PAYLOAD)
    return DATA_BIND_ERR_SCHEMA;

  name = entry_logical_name(entry);
  *present = 1;
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
    value = 4u;
  } else if (strcmp(name, "scale") == 0) {
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
    const void *value, size_t value_bytes, DataBindError *error) {
  TestProvider *provider = (TestProvider *)context;
  (void)error;
  ++provider->write_calls;
  if (provider->fail_write) return DATA_BIND_ERR_RUNTIME;
  if (entry->address.binding_class != DATA_BIND_BINDING_RESULT ||
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

spec("DataBind canonical Service BindingPlan") {
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
    check_equal(data_bind_binding_plan_error_at(http_plan, 0u), "CalcError");

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
    check_equal(request.presence, 0u);

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

  it("rejects malformed native optional-presence bindings") {
    DataBind *codec = create_codec();
    ProjectionScratch scratch = {{0}, {0}};
    DataBindBindingProjection http =
        projection("http-v1", &scratch, http_project);
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;

    const DataBindNativePresenceBinding required_presence[] = {
        {sizeof(DataBindNativePresenceBinding), "left",
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
      const DataBindNativePresenceBinding duplicate_presence[] = {
          {sizeof(DataBindNativePresenceBinding), "scale",
           offsetof(AddRequest, presence), 0u},
          {sizeof(DataBindNativePresenceBinding), "scale",
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
      const DataBindNativePresenceBinding overlapping_presence[] = {
          {sizeof(DataBindNativePresenceBinding), "scale",
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

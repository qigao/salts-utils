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

static const cmeta_type_identity ADD_REQUEST_IDENTITY =
    CMETA_TYPE_ID_ATOM_INIT("test.binding.AddRequest");
static const cmeta_type_desc ADD_REQUEST_TYPE = {
    "AddRequest", sizeof(AddRequest), _Alignof(AddRequest), CMETA_T_OBJECT,
    NULL, NULL, &ADD_REQUEST_IDENTITY};
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
static const cmeta_data_field_desc ADD_REQUEST_DATA_FIELDS[] = {
    {"test.binding.AddRequest.left", "left", offsetof(AddRequest, left),
     &salts_uint32_cmeta_data},
    {"test.binding.AddRequest.right", "right", offsetof(AddRequest, right),
     &salts_uint32_cmeta_data},
    {"test.binding.AddRequest.scale", "scale", offsetof(AddRequest, scale),
     &salts_uint32_cmeta_data}};
static const cmeta_data_struct_shape ADD_REQUEST_SHAPE = {
    &ADD_REQUEST_LAYOUT, ADD_REQUEST_DATA_FIELDS, 3u};
static const cmeta_data_desc ADD_REQUEST_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.binding.AddRequest.data",
    .display_name = "AddRequest",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &ADD_REQUEST_TYPE,
    .shape = &ADD_REQUEST_SHAPE};

static const DataBindNativePresenceBinding ADD_REQUEST_PRESENCE[] = {
    {sizeof(DataBindNativePresenceBinding), "scale",
     offsetof(AddRequest, presence), 0u}};
static const DataBindNativeRecordBinding ADD_REQUEST_BINDING = {
    sizeof(DataBindNativeRecordBinding),
    DATA_BIND_BINDING_PLAN_ABI_VERSION,
    "AddRequest",
    &ADD_REQUEST_DATA,
    ADD_REQUEST_PRESENCE,
    1u};

static const cmeta_type_identity ADD_RESPONSE_IDENTITY =
    CMETA_TYPE_ID_ATOM_INIT("test.binding.AddResponse");
static const cmeta_type_desc ADD_RESPONSE_TYPE = {
    "AddResponse", sizeof(AddResponse), _Alignof(AddResponse), CMETA_T_OBJECT,
    NULL, NULL, &ADD_RESPONSE_IDENTITY};
static const cmeta_field_desc ADD_RESPONSE_LAYOUT_FIELDS[] = {
    {"sum", "uint32_t", offsetof(AddResponse, sum), sizeof(uint32_t),
     _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL}};
static const cmeta_struct_desc ADD_RESPONSE_LAYOUT = {
    "AddResponse", sizeof(AddResponse), _Alignof(AddResponse),
    ADD_RESPONSE_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc ADD_RESPONSE_DATA_FIELDS[] = {
    {"test.binding.AddResponse.sum", "sum", offsetof(AddResponse, sum),
     &salts_uint32_cmeta_data}};
static const cmeta_data_struct_shape ADD_RESPONSE_SHAPE = {
    &ADD_RESPONSE_LAYOUT, ADD_RESPONSE_DATA_FIELDS, 1u};
static const cmeta_data_desc ADD_RESPONSE_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.binding.AddResponse.data",
    .display_name = "AddResponse",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &ADD_RESPONSE_TYPE,
    .shape = &ADD_RESPONSE_SHAPE};
static const DataBindNativeRecordBinding ADD_RESPONSE_BINDING = {
    sizeof(DataBindNativeRecordBinding),
    DATA_BIND_BINDING_PLAN_ABI_VERSION,
    "AddResponse",
    &ADD_RESPONSE_DATA,
    NULL,
    0u};

static const cmeta_type_desc UINT32_POINTER_TYPE = {
    "uint32_t *", sizeof(uint32_t *), _Alignof(uint32_t *), CMETA_T_POINTER,
    &salts_uint32_cmeta_type, NULL, NULL};
static const cmeta_type_desc ADD_RESPONSE_POINTER_TYPE = {
    "AddResponse *", sizeof(AddResponse *), _Alignof(AddResponse *),
    CMETA_T_POINTER, &ADD_RESPONSE_TYPE, NULL, NULL};

FunctionDeclAs(
    value, void, &cmeta_type_void, binding_add_fields,
    (uint32_t, left, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, right, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, scale, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t *, sum, CMETA_PARAM_OUT, &UINT32_POINTER_TYPE));

FunctionDeclAs(
    value, void, &cmeta_type_void, binding_add_root,
    (AddRequest, request, CMETA_PARAM_IN, &ADD_REQUEST_TYPE),
    (AddResponse *, response, CMETA_PARAM_OUT, &ADD_RESPONSE_POINTER_TYPE));

FunctionDeclAs(
    value, AddResponse, &ADD_RESPONSE_TYPE, binding_add_root_return,
    (AddRequest, request, CMETA_PARAM_IN, &ADD_REQUEST_TYPE));

FunctionDeclAs(
    value, void, &cmeta_type_void, binding_add_bad_type,
    (int32_t, left, CMETA_PARAM_IN, &salts_int32_cmeta_type),
    (uint32_t, right, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, scale, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t *, sum, CMETA_PARAM_OUT, &UINT32_POINTER_TYPE));

FunctionDeclAs(
    value, void, &cmeta_type_void, binding_add_bad_direction,
    (uint32_t, left, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, right, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t, scale, CMETA_PARAM_IN, &salts_uint32_cmeta_type),
    (uint32_t *, sum, CMETA_PARAM_IN, &UINT32_POINTER_TYPE));

typedef struct ProjectionContext {
  char selector[128];
} ProjectionContext;

static DataBindStatus http_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingProjectionSlot *out, DataBindError *error) {
  ProjectionContext *state = (ProjectionContext *)context;
  const char *prefix = NULL;
  (void)error;

  if (operation == NULL || field == NULL || out == NULL || state == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (direction == DATA_BIND_BINDING_EGRESS) {
    out->binding_class = DATA_BIND_BINDING_RESULT;
    snprintf(state->selector, sizeof(state->selector), "result:%s",
             field->name);
    out->selector = state->selector;
    return DATA_BIND_OK;
  }

  if (field->binding_kind == NULL)
    return DATA_BIND_ERR_SCHEMA;
  if (strcmp(field->binding_kind, "path") == 0 ||
      strcmp(field->binding_kind, "query") == 0) {
    out->binding_class = DATA_BIND_BINDING_VALUE;
    prefix = field->binding_kind;
  } else if (strcmp(field->binding_kind, "header") == 0 ||
             strcmp(field->binding_kind, "cookie") == 0) {
    out->binding_class = DATA_BIND_BINDING_METADATA;
    prefix = field->binding_kind;
  } else if (strcmp(field->binding_kind, "body") == 0) {
    out->binding_class = DATA_BIND_BINDING_PAYLOAD;
    prefix = "body";
  } else {
    return DATA_BIND_ERR_SCHEMA;
  }

  snprintf(state->selector, sizeof(state->selector), "%s:%s", prefix,
           field->binding_name != NULL ? field->binding_name : field->name);
  out->selector = state->selector;
  return DATA_BIND_OK;
}

static DataBindStatus rpc_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingProjectionSlot *out, DataBindError *error) {
  ProjectionContext *state = (ProjectionContext *)context;
  (void)operation;
  (void)error;
  if (state == NULL || field == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  out->binding_class = direction == DATA_BIND_BINDING_INGRESS
                           ? DATA_BIND_BINDING_VALUE
                           : DATA_BIND_BINDING_RESULT;
  snprintf(state->selector, sizeof(state->selector), "%s:%s",
           direction == DATA_BIND_BINDING_INGRESS ? "param" : "result",
           field->name);
  out->selector = state->selector;
  return DATA_BIND_OK;
}

static DataBindStatus mqtt_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingProjectionSlot *out, DataBindError *error) {
  ProjectionContext *state = (ProjectionContext *)context;
  (void)operation;
  (void)error;
  if (state == NULL || field == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (direction == DATA_BIND_BINDING_EGRESS) {
    out->binding_class = DATA_BIND_BINDING_RESULT;
    snprintf(state->selector, sizeof(state->selector), "reply:%s",
             field->name);
  } else if (strcmp(field->name, "left") == 0) {
    out->binding_class = DATA_BIND_BINDING_VALUE;
    snprintf(state->selector, sizeof(state->selector), "topic:%s",
             field->name);
  } else if (strcmp(field->name, "right") == 0) {
    out->binding_class = DATA_BIND_BINDING_PAYLOAD;
    snprintf(state->selector, sizeof(state->selector), "payload:%s",
             field->name);
  } else {
    out->binding_class = DATA_BIND_BINDING_METADATA;
    snprintf(state->selector, sizeof(state->selector), "property:%s",
             field->name);
  }
  out->selector = state->selector;
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

static const char *selector_name(const char *selector) {
  const char *colon;
  if (selector == NULL) return NULL;
  colon = strrchr(selector, ':');
  return colon != NULL ? colon + 1 : selector;
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
  if (entry->binding_class != DATA_BIND_BINDING_VALUE &&
      entry->binding_class != DATA_BIND_BINDING_METADATA &&
      entry->binding_class != DATA_BIND_BINDING_PAYLOAD)
    return DATA_BIND_ERR_SCHEMA;

  name = selector_name(entry->selector);
  *present = 1;
  if (name != NULL && strcmp(name, "left") == 0) {
    value = 3u;
  } else if (name != NULL && strcmp(name, "right") == 0) {
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
  if (entry->binding_class != DATA_BIND_BINDING_RESULT ||
      strcmp(entry->logical_name, "sum") != 0 ||
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

static DataBindNativeOptions native_options(unsigned char *workspace,
                                             size_t workspace_bytes) {
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  options.workspace = workspace;
  options.workspace_bytes = workspace_bytes;
  options.max_depth = 16u;
  options.max_items = 64u;
  options.max_owned_bytes = 1024u;
  return options;
}

static DataBind *create_codec(void) {
  static const char schema[] =
      "message AddRequest {"
      " [query] uint32 left;"
      " [query] uint32 right;"
      " optional [query] uint32 scale default 1;"
      "}"
      "message AddResponse { uint32 sum; }"
      "service Calc {"
      " [GET(\"/add\"), rpc]"
      " Add: AddRequest -> AddResponse;"
      "}";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  check_equal(data_bind_create_from_text(
                  schema, sizeof(schema) - 1u, &codec, &error),
              DATA_BIND_OK);
  return codec;
}

spec("DataBind transport-neutral BindingPlan") {
  it("compiles HTTP RPC and MQTT projections from the same service contract") {
    DataBind *codec = create_codec();
    ProjectionContext http_context = {{0}};
    ProjectionContext rpc_context = {{0}};
    ProjectionContext mqtt_context = {{0}};
    DataBindBindingProjection http =
        projection("http-v1", &http_context, http_project);
    DataBindBindingProjection rpc =
        projection("rpc-v1", &rpc_context, rpc_project);
    DataBindBindingProjection mqtt =
        projection("mqtt-v1", &mqtt_context, mqtt_project);
    DataBindServiceNativeBinding native =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_fields),
            &ADD_REQUEST_BINDING, &ADD_RESPONSE_BINDING);
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
    check_equal(data_bind_binding_plan_ingress_count(http_plan), 3u);
    check_equal(data_bind_binding_plan_egress_count(http_plan), 1u);

    check(data_bind_binding_plan_ingress_at(http_plan, 0u, &entry) == 1);
    check_true(entry.binding_class == DATA_BIND_BINDING_VALUE);
    check_equal(entry.selector, "query:left");
    check_equal(entry.native_offset, 0u);

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_ingress_at(mqtt_plan, 1u, &entry) == 1);
    check_true(entry.binding_class == DATA_BIND_BINDING_PAYLOAD);
    check_equal(entry.selector, "payload:right");

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_ingress_at(mqtt_plan, 2u, &entry) == 1);
    check_true(entry.binding_class == DATA_BIND_BINDING_METADATA);
    check_equal(entry.selector, "property:scale");

    data_bind_binding_plan_free(mqtt_plan);
    data_bind_binding_plan_free(rpc_plan);
    data_bind_binding_plan_free(http_plan);
    data_bind_free(codec);
  }

  it("executes a compiled plan after the schema codec is released") {
    DataBind *codec = create_codec();
    ProjectionContext context = {{0}};
    DataBindBindingProjection http =
        projection("http-v1", &context, http_project);
    DataBindServiceNativeBinding native =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_fields),
            &ADD_REQUEST_BINDING, &ADD_RESPONSE_BINDING);
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
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    data_bind_free(codec);
    codec = NULL;

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

    check_equal(data_bind_native_clear(
                    &options, &salts_uint32_cmeta_data,
                    &left, sizeof(left), &native_diagnostic),
                DATA_BIND_OK);
    native_diagnostic =
        (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_native_clear(
                    &options, &salts_uint32_cmeta_data,
                    &right, sizeof(right), &native_diagnostic),
                DATA_BIND_OK);
    native_diagnostic =
        (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_native_clear(
                    &options, &salts_uint32_cmeta_data,
                    &scale, sizeof(scale), &native_diagnostic),
                DATA_BIND_OK);
    native_diagnostic =
        (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_native_clear(
                    &options, &salts_uint32_cmeta_data,
                    &sum, sizeof(sum), &native_diagnostic),
                DATA_BIND_OK);

    data_bind_binding_plan_free(plan);
  }

  it("rolls back field-mapped parameter staging after decode failure") {
    DataBind *codec = create_codec();
    ProjectionContext context = {{0}};
    DataBindBindingProjection http =
        projection("http-v1", &context, http_project);
    DataBindServiceNativeBinding native =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_fields),
            &ADD_REQUEST_BINDING, &ADD_RESPONSE_BINDING);
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    TestProvider state = {.fail_right_type = 1};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    uint32_t left = 91u, right = 92u, scale = 93u, sum = 94u;
    void *params[] = {&left, &right, &scale, &sum};
    const size_t param_bytes[] = {
        sizeof(left), sizeof(right), sizeof(scale), sizeof(sum)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 4u;

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(diagnostic.schema_field, "right");
    check_equal(diagnostic.function_param, "right");
    check_equal(left, 0u);
    check_equal(right, 0u);
    check_equal(scale, 0u);
    check_equal(sum, 0u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("uses root request plus caller-owned OUT response and resets presence on failure") {
    DataBind *codec = create_codec();
    ProjectionContext context = {{0}};
    DataBindBindingProjection http =
        projection("http-v1", &context, http_project);
    DataBindServiceNativeBinding native =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_root),
            &ADD_REQUEST_BINDING, &ADD_RESPONSE_BINDING);
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    DataBindBindingPlanEntry entry = DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    TestProvider state = {.provide_scale = 1};
    DataBindBindingProvider provider = provider_for(&state);
    unsigned char workspace[4096];
    DataBindNativeOptions options =
        native_options(workspace, sizeof(workspace));
    AddRequest request = {0};
    AddResponse response = {0};
    void *params[] = {NULL, &response};
    const size_t param_bytes[] = {0u, sizeof(response)};
    DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &native,
                    &plan, &diagnostic),
                DATA_BIND_OK);
    check(data_bind_binding_plan_ingress_at(plan, 1u, &entry) == 1);
    check_true(entry.target == DATA_BIND_BINDING_TARGET_REQUEST_FIELD);
    check_equal(entry.native_offset, offsetof(AddRequest, right));

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
    check_equal(request.scale, 2u);
    check_true((request.presence & 1u) != 0u);

    response.sum = request.left + request.right * request.scale;
    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(state.published_sum, 11u);

    {
      DataBindNativeDiagnostic d = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
      check_equal(data_bind_native_clear(
                      &options, &ADD_REQUEST_DATA, &request,
                      sizeof(request), &d),
                  DATA_BIND_OK);
      request.presence = 0u;
      d = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
      check_equal(data_bind_native_clear(
                      &options, &ADD_RESPONSE_DATA, &response,
                      sizeof(response), &d),
                  DATA_BIND_OK);
    }

    state = (TestProvider){.fail_right_type = 1, .provide_scale = 1};
    provider = provider_for(&state);
    request = (AddRequest){
        .left = 91u, .right = 92u, .scale = 93u, .presence = 0xffu};
    response = (AddResponse){.sum = 94u};

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &options, &frame, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
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
    ProjectionContext context = {{0}};
    DataBindBindingProjection http =
        projection("http-v1", &context, http_project);
    DataBindServiceNativeBinding native =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_fields),
            &ADD_REQUEST_BINDING, &ADD_RESPONSE_BINDING);
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
                    codec, "Calc", "Add", &http, &native,
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

  it("rejects native type direction presence and return-ownership mismatches") {
    DataBind *codec = create_codec();
    ProjectionContext context = {{0}};
    DataBindBindingProjection http =
        projection("http-v1", &context, http_project);
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    DataBindServiceNativeBinding bad_type =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_bad_type),
            &ADD_REQUEST_BINDING, &ADD_RESPONSE_BINDING);
    DataBindServiceNativeBinding bad_direction =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_bad_direction),
            &ADD_REQUEST_BINDING, &ADD_RESPONSE_BINDING);
    DataBindNativeRecordBinding missing_presence = ADD_REQUEST_BINDING;
    DataBindServiceNativeBinding bad_presence =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_fields),
            &missing_presence, &ADD_RESPONSE_BINDING);
    DataBindServiceNativeBinding unsafe_return =
        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
            FunctionMeta(binding_add_root_return),
            &ADD_REQUEST_BINDING, &ADD_RESPONSE_BINDING);

    missing_presence.presence = NULL;
    missing_presence.presence_count = 0u;

    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &bad_type,
                    &plan, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(plan);
    check_equal(diagnostic.schema_field, "left");

    diagnostic =
        (DataBindBindingPlanDiagnostic)DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &bad_direction,
                    &plan, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(plan);
    check_equal(diagnostic.function_param, "sum");

    diagnostic =
        (DataBindBindingPlanDiagnostic)DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &bad_presence,
                    &plan, &diagnostic),
                DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_equal(diagnostic.schema_field, "scale");

    diagnostic =
        (DataBindBindingPlanDiagnostic)DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &http, &unsafe_return,
                    &plan, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(plan);

    data_bind_free(codec);
  }
}

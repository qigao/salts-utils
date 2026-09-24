#include <salts/plugin.h>
#include <tinytest.h>

#include "data_bind_binding_plan.h"
#include "image_binding_native.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef GENERATED_DATABIND_PLUGIN_PATH
#error "GENERATED_DATABIND_PLUGIN_PATH is required"
#endif

typedef struct PluginTokenReader {
  cserde_token token;
  int emitted;
} PluginTokenReader;

static cserde_status plugin_token_next(
    void *context, cserde_token *out) {
  PluginTokenReader *reader = (PluginTokenReader *)context;
  if (reader == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (reader->emitted) return CSERDE_DONE;
  *out = reader->token;
  reader->emitted = 1;
  return CSERDE_OK;
}

static const cserde_reader_ops PLUGIN_TOKEN_READER_OPS = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    plugin_token_next};

typedef struct PluginBindingProvider {
  PluginTokenReader reader;
  uint32_t input_width;
  uint32_t staged_pixels;
  uint32_t published_pixels;
  size_t begin_calls;
  size_t write_calls;
  size_t commit_calls;
  size_t abort_calls;
} PluginBindingProvider;

static DataBindStatus plugin_test_project_field(
    void *context,
    const DataBindServiceOperation *operation,
    const DataBindSchemaField *field,
    DataBindBindingDirection direction,
    DataBindBindingAddress *out,
    DataBindError *error) {
  (void)context;
  (void)operation;
  (void)error;

  if (field == NULL || out == NULL) return DATA_BIND_ERR_INVALID_ARG;

  *out = (DataBindBindingAddress)DATA_BIND_BINDING_ADDRESS_INIT;
  out->binding_class =
      direction == DATA_BIND_BINDING_INGRESS
          ? DATA_BIND_BINDING_VALUE
          : DATA_BIND_BINDING_RESULT;
  out->space = "generated-plugin-test";
  out->name = field->name;
  out->ordinal = 0u;
  return DATA_BIND_OK;
}

static DataBindStatus plugin_provider_open_input(
    void *context,
    const DataBindBindingPlanEntry *entry,
    cserde_reader *reader,
    DataBindBindingValueState *state,
    DataBindError *error) {
  PluginBindingProvider *provider =
      (PluginBindingProvider *)context;
  (void)error;

  if (provider == NULL || entry == NULL ||
      reader == NULL || state == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (entry->address.binding_class != DATA_BIND_BINDING_VALUE ||
      entry->schema_field == NULL ||
      strcmp(entry->schema_field, "width") != 0)
    return DATA_BIND_ERR_SCHEMA;

  provider->reader.token =
      (cserde_token){
          .kind = CSERDE_UINT,
          .value.uint = provider->input_width};
  provider->reader.emitted = 0;
  *state = DATA_BIND_STATE_VALUE;

  return cserde_reader_init(
             reader,
             &PLUGIN_TOKEN_READER_OPS,
             &provider->reader) == CSERDE_OK
             ? DATA_BIND_OK
             : DATA_BIND_ERR_RUNTIME;
}

static DataBindStatus plugin_provider_begin_output(
    void *context, DataBindError *error) {
  PluginBindingProvider *provider =
      (PluginBindingProvider *)context;
  (void)error;
  if (provider == NULL) return DATA_BIND_ERR_INVALID_ARG;
  ++provider->begin_calls;
  provider->staged_pixels = 0u;
  return DATA_BIND_OK;
}

static DataBindStatus plugin_provider_write_output(
    void *context,
    const DataBindBindingPlanEntry *entry,
    DataBindBindingValueState state,
    const void *value,
    size_t value_bytes,
    DataBindError *error) {
  PluginBindingProvider *provider =
      (PluginBindingProvider *)context;
  (void)error;

  if (provider == NULL || entry == NULL ||
      state != DATA_BIND_STATE_VALUE || value == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (entry->address.binding_class != DATA_BIND_BINDING_RESULT ||
      entry->schema_field == NULL ||
      strcmp(entry->schema_field, "pixels") != 0 ||
      value_bytes != sizeof(uint32_t))
    return DATA_BIND_ERR_TYPE_MISMATCH;

  ++provider->write_calls;
  provider->staged_pixels = *(const uint32_t *)value;
  return DATA_BIND_OK;
}

static DataBindStatus plugin_provider_commit_output(
    void *context, DataBindError *error) {
  PluginBindingProvider *provider =
      (PluginBindingProvider *)context;
  (void)error;
  if (provider == NULL) return DATA_BIND_ERR_INVALID_ARG;
  ++provider->commit_calls;
  provider->published_pixels = provider->staged_pixels;
  return DATA_BIND_OK;
}

static void plugin_provider_abort_output(void *context) {
  PluginBindingProvider *provider =
      (PluginBindingProvider *)context;
  if (provider == NULL) return;
  ++provider->abort_calls;
  provider->staged_pixels = 0u;
}

static DataBindBindingProvider plugin_provider(
    PluginBindingProvider *state) {
  DataBindBindingProvider provider =
      DATA_BIND_BINDING_PROVIDER_INIT;
  provider.context = state;
  provider.open_input = plugin_provider_open_input;
  provider.begin_output = plugin_provider_begin_output;
  provider.write_output = plugin_provider_write_output;
  provider.commit_output = plugin_provider_commit_output;
  provider.abort_output = plugin_provider_abort_output;
  return provider;
}

spec("generated DataBind Plugin Service") {
  it("executes BindingPlan -> Plugin -> BindingPlan under one DSO lease") {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {.capacity = 2u};
    salts_plugin_ref ref = {0};
    salts_plugin_lease lease = {0};
    const salts_plugin_manifest *manifest = NULL;
    const salts_plugin_export *entry = NULL;
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *request_data = NULL;
    const cmeta_data_desc *response_data = NULL;
    DataBindNativeTypeBinding request_native;
    DataBindNativeTypeBinding response_native;
    DataBindServiceNativeBinding native;
    DataBindBindingProjection projection = {
        sizeof(DataBindBindingProjection),
        DATA_BIND_BINDING_PLAN_ABI_VERSION,
        "generated-plugin-test",
        NULL,
        plugin_test_project_field,
    };
    DataBindBindingPlan *plan = NULL;
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    PluginBindingProvider provider_state = {0};
    DataBindBindingProvider provider =
        plugin_provider(&provider_state);
    unsigned char workspace[4096];
    DataBindNativeOptions native_options =
        DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DecodeRequest_t request = {0};
    DecodeResponse_t response = {0};
    void *frame_params[] = {NULL, &response};
    const size_t frame_param_bytes[] = {0u, sizeof(response)};
    DataBindBindingCallFrame frame =
        DATA_BIND_BINDING_CALL_FRAME_INIT;
    void *invoke_params[] = {&request, &response};
    int native_status = -99;
    bool quiescent = false;

    provider_state.input_width = 12u;
    native_options.workspace = workspace;
    native_options.workspace_bytes = sizeof(workspace);
    native_options.max_depth = 16u;
    native_options.max_items = 64u;
    native_options.max_owned_bytes = 1024u;

    check_equal(salts_plugin_registry_init(&registry, &config),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_load(
                    &registry, GENERATED_DATABIND_PLUGIN_PATH, &ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_start(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_acquire(
                    &registry, ref, &lease, &manifest),
                SALTS_PLUGIN_OK);

    check_not_null(manifest);
    check_equal(manifest->plugin_id, "Image.ImageProcessor");
    check_equal(manifest->export_count, (size_t)2u);
    check_equal(salts_plugin_manifest_find_export(
                    manifest, "Image.Codec.Decode", &entry),
                SALTS_PLUGIN_OK);
    {
      const salts_plugin_export *unselected = NULL;
      check_equal(salts_plugin_manifest_find_export(
                      manifest, "Image.Admin.Inspect", &unselected),
                  SALTS_PLUGIN_UNKNOWN_EXPORT);
      check_null(unselected);
    }
    check_not_null(entry);
    check_equal(entry->kind, SALTS_PLUGIN_EXPORT_FUNCTION);
    check_equal(salts_plugin_export_require_function(
                    entry, "Image.Codec", 1u, 0u),
                SALTS_PLUGIN_OK);
    check_true(cmeta_function_desc_valid(entry->value.function.desc));
    check_true(cmeta_function_abi_desc_valid(entry->value.function.abi));
    check_true(entry->value.function.abi->function ==
               entry->value.function.desc);
    check_equal(entry->value.function.abi->return_carrier,
                CMETA_ABI_SCALAR);
    check_equal(cmeta_function_param_abi(
                    entry->value.function.abi, 0u),
                CMETA_ABI_OBJECT_POINTER);
    check_equal(cmeta_function_param_abi(
                    entry->value.function.abi, 1u),
                CMETA_ABI_OBJECT_POINTER);

    /*
     * The same canonical IDL generated independent host-side native CMeta
     * descriptors. BindingPlan admits the Plugin FunctionDesc by semantic
     * type identity rather than descriptor address.
     */
    check_equal(Image_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    check_equal(DecodeRequest_cmeta_data(&request_data, &error),
                DATA_BIND_OK);
    check_equal(DecodeResponse_cmeta_data(&response_data, &error),
                DATA_BIND_OK);
    check_not_null(request_data);
    check_not_null(response_data);

    request_native =
        (DataBindNativeTypeBinding)
            DATA_BIND_NATIVE_TYPE_BINDING_INIT(
                "DecodeRequest", request_data);
    response_native =
        (DataBindNativeTypeBinding)
            DATA_BIND_NATIVE_TYPE_BINDING_INIT(
                "DecodeResponse", response_data);
    native =
        (DataBindServiceNativeBinding)
            DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
                entry->value.function.desc,
                &request_native,
                &response_native);

    check_equal(data_bind_binding_plan_compile_service(
                    codec,
                    "Codec",
                    "Decode",
                    &projection,
                    &native,
                    &plan,
                    &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    check_true(data_bind_binding_plan_function(plan) ==
               entry->value.function.desc);
    check_equal(data_bind_binding_plan_ingress_count(plan), (size_t)1u);
    check_equal(data_bind_binding_plan_egress_count(plan), (size_t)1u);

    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.return_value = &native_status;
    frame.return_bytes = sizeof(native_status);
    frame.params = frame_params;
    frame.param_bytes = frame_param_bytes;
    frame.param_count =
        sizeof(frame_params) / sizeof(frame_params[0]);

    check_equal(data_bind_binding_plan_bind_inputs(
                    plan, &provider, &native_options,
                    &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(request.width, 12u);

    /*
     * Root request storage is represented separately in BindingCallFrame.
     * The declaration-specific Plugin adapter receives the concrete argument
     * vector for the actual C function call.
     */
    invoke_params[0] = frame.request;
    invoke_params[1] = frame.params[1];
    check_true(entry->value.function.invoke(
        entry->value.function.context,
        frame.return_value, invoke_params, 2u));
    check_equal(native_status, 0);
    check_equal(response.pixels, 48u);

    check_equal(data_bind_binding_plan_write_outputs(
                    plan, &provider, &frame, &diagnostic),
                DATA_BIND_OK);
    check_equal(provider_state.begin_calls, (size_t)1u);
    check_equal(provider_state.write_calls, (size_t)1u);
    check_equal(provider_state.commit_calls, (size_t)1u);
    check_equal(provider_state.abort_calls, (size_t)0u);
    check_equal(provider_state.published_pixels, 48u);

    /*
     * FunctionDesc/DataDesc/traits are Plugin/DSO-borrowed semantics. Destroy
     * the plan and clean native staging while the Plugin lease is still live.
     */
    data_bind_binding_plan_free(plan);
    plan = NULL;
    check_equal(data_bind_native_clear(
                    &native_options, request_data,
                    &request, sizeof(request),
                    &native_diagnostic),
                DATA_BIND_OK);
    native_diagnostic =
        (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_native_clear(
                    &native_options, response_data,
                    &response, sizeof(response),
                    &native_diagnostic),
                DATA_BIND_OK);
    data_bind_free(codec);
    codec = NULL;

    check_equal(salts_plugin_registry_release(&registry, &lease),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_request_stop(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_poll_quiescent(
                    &registry, ref, &quiescent),
                SALTS_PLUGIN_OK);
    check_true(quiescent);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_destroy(&registry),
                SALTS_PLUGIN_OK);
  }
}

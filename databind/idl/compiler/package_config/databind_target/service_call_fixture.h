#ifndef INSTALLED_SERVICE_CALL_FIXTURE_H
#define INSTALLED_SERVICE_CALL_FIXTURE_H

#include <data_bind_binding_plan.h>
#include "installed_service.service_native.h"
#include "installed_service_native.h"
#include <string.h>

typedef struct InstalledCallInput {
  cserde_token token;
  int emitted;
} InstalledCallInput;

static cserde_status installed_call_next(void *context, cserde_token *out) {
  InstalledCallInput *input = (InstalledCallInput *)context;
  if (input->emitted) return CSERDE_DONE;
  *out = input->token;
  input->emitted = 1;
  return CSERDE_OK;
}

static DataBindStatus installed_call_open(
    void *context, const DataBindBindingPlanEntry *entry,
    cserde_reader *reader, DataBindBindingValueState *state,
    DataBindError *error) {
  static const cserde_reader_ops ops = {
      sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION, installed_call_next};
  InstalledCallInput *input = (InstalledCallInput *)context;
  (void)error;
  if (strcmp(entry->schema_field, "scale") == 0) {
    *state = DATA_BIND_VALUE_STATE_ABSENT;
    return DATA_BIND_OK;
  }
  input->token.kind = CSERDE_UINT;
  input->token.value.uint = 3u;
  input->emitted = 0;
  *state = DATA_BIND_VALUE_STATE_VALUE;
  return cserde_reader_init(reader, &ops, input) == CSERDE_OK
             ? DATA_BIND_OK : DATA_BIND_ERR_RUNTIME;
}

static DataBindStatus installed_call_project(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  (void)context;
  (void)operation;
  (void)error;
  out->binding_class = direction == DATA_BIND_BINDING_INGRESS
                           ? DATA_BIND_BINDING_VALUE : DATA_BIND_BINDING_RESULT;
  out->space = "installed-call";
  out->name = field->name;
  return DATA_BIND_OK;
}

/* One installed C/C++ contract, linked solely through the public SDK target. */
static int installed_service_call(void) {
  DataBind *codec = NULL;
  DataBindBindingPlan *plan = NULL;
  DataBindNativeTypeBinding request_binding = {0};
  DataBindNativeTypeBinding response_binding = {0};
  DataBindServiceNativeBinding native = {0};
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindBindingPlanDiagnostic diagnostic = DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
  DataBindBindingProjection projection = DATA_BIND_BINDING_PROJECTION_INIT;
  DataBindBindingProvider provider = DATA_BIND_BINDING_PROVIDER_INIT;
  DataBindBindingCallLifetime lifetime = DATA_BIND_BINDING_CALL_LIFETIME_INIT;
  DataBindBindingCallFrame frame = DATA_BIND_BINDING_CALL_FRAME_INIT;
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  InstalledCallInput input = {{CSERDE_UINT}, 0};
  unsigned char workspace[4096];
  AddRequest_t request = {0};
  AddResponse_t response = {0};
  void *params[] = {&request, &response};
  const size_t sizes[] = {sizeof(request), sizeof(response)};
  int result = 1;

  if (databind_10_ServiceSdk_4_Calc_3_Add__databind_native_binding(
      &request_binding, &response_binding, &native, &error) != DATA_BIND_OK ||
      ServiceSdk_codec_create(&codec, &error) != DATA_BIND_OK) goto cleanup;
  projection.id = "installed-call";
  projection.project_field = installed_call_project;
  if (data_bind_binding_plan_compile_service(codec, "Calc", "Add", &projection,
      &native, &plan, &diagnostic) != DATA_BIND_OK) goto cleanup;
  data_bind_free(codec);
  codec = NULL;
  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = 16u;
  options.max_items = 64u;
  options.max_owned_bytes = sizeof(workspace);
  provider.context = &input;
  provider.open_input = installed_call_open;
  frame.request = &request;
  frame.request_bytes = sizeof(request);
  frame.params = params;
  frame.param_bytes = sizes;
  frame.param_count = 2u;
  if (data_bind_binding_plan_bind_call(plan, &provider, &options, &frame,
      &lifetime, &diagnostic) != DATA_BIND_OK) goto cleanup;
  if (!data_bind_binding_call_is_live(&lifetime) || request.left != 3u ||
      request.scale != 1u ||
      databind_10_ServiceSdk_4_Calc_3_Add(&request, &response) != 0 ||
      response.sum != 4u) goto cleanup;
  if (data_bind_binding_call_restore_zero(&lifetime, &diagnostic) != DATA_BIND_OK ||
      request.left != 0u || request.scale != 0u || response.sum != 0u ||
      data_bind_binding_call_is_live(&lifetime) ||
      data_bind_binding_call_restore_zero(&lifetime, &diagnostic) !=
          DATA_BIND_ERR_INVALID_ARG) goto cleanup;
  result = 0;
cleanup:
  if (data_bind_binding_call_is_live(&lifetime))
    if (data_bind_binding_call_restore_zero(&lifetime, &diagnostic) != DATA_BIND_OK)
      result = 1;
  data_bind_binding_plan_free(plan);
  data_bind_free(codec);
  return result;
}

#endif

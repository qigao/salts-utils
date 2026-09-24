#include "data_bind_binding_plan.h"
#include "data_bind_method_plan.h"
#include "service_native_generated.h"
#include "tinytest.h"

#include <cmeta/function.h>
#include <string.h>

const cmeta_function_desc *
databind_13_ServiceNative_4_Calc_3_Add__databind_function(void);
const cmeta_function_abi_desc *
databind_13_ServiceNative_4_Calc_3_Add__databind_function_abi(void);
const cmeta_function_desc *
databind_13_ServiceNative_4_Calc_4_Find__databind_function(void);
const cmeta_function_abi_desc *
databind_13_ServiceNative_4_Calc_4_Find__databind_function_abi(void);
DataBindStatus databind_13_ServiceNative_4_Calc_3_Add__databind_native_binding(
    DataBindNativeTypeBinding *request_out,
    DataBindNativeTypeBinding *response_out,
    DataBindServiceNativeBinding *service_out,
    DataBindError *error);
DataBindStatus databind_13_ServiceNative_4_Calc_4_Find__databind_native_binding(
    DataBindNativeTypeBinding *request_out,
    DataBindNativeTypeBinding *response_out,
    DataBindServiceNativeBinding *service_out,
    DataBindError *error);

int databind_13_ServiceNative_4_Calc_4_Find(
    const AddRequest_t *request,
    AddResponse_t *response,
    databind_13_ServiceNative_4_Calc_4_Find__error *error);

static DataBindStatus project_field(
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
  out->space = "native-test";
  out->name = field->name;
  return DATA_BIND_OK;
}

typedef struct OutcomeProviderState {
  size_t begin_calls;
  size_t write_calls;
  size_t result_calls;
  size_t error_calls;
  size_t commit_calls;
  size_t abort_calls;
  uint32_t result_value;
  uint32_t error_value;
  const char *error_name;
  int fail_write;
} OutcomeProviderState;

static DataBindStatus outcome_begin(void *context, DataBindError *error) {
  OutcomeProviderState *state = (OutcomeProviderState *)context;
  (void)error;
  if (state == NULL) return DATA_BIND_ERR_INVALID_ARG;
  ++state->begin_calls;
  return DATA_BIND_OK;
}

static DataBindStatus outcome_write(
    void *context, const DataBindBindingPlanEntry *entry,
    DataBindBindingValueState value_state, const void *value,
    size_t value_bytes, DataBindError *error) {
  OutcomeProviderState *state = (OutcomeProviderState *)context;
  (void)error;
  if (state == NULL || entry == NULL) return DATA_BIND_ERR_INVALID_ARG;
  ++state->write_calls;
  if (state->fail_write) return DATA_BIND_ERR_RUNTIME;
  if (value_state != DATA_BIND_VALUE_STATE_VALUE || value == NULL)
    return DATA_BIND_ERR_TYPE_MISMATCH;

  if (entry->address.binding_class == DATA_BIND_BINDING_RESULT) {
    if (entry->schema_field == NULL ||
        strcmp(entry->schema_field, "sum") != 0 ||
        value_bytes != sizeof(uint32_t))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    state->result_value = *(const uint32_t *)value;
    ++state->result_calls;
    return DATA_BIND_OK;
  }

  if (entry->address.binding_class == DATA_BIND_BINDING_ERROR) {
    if (entry->address.name == NULL) return DATA_BIND_ERR_SCHEMA;
    state->error_name = entry->address.name;
    if (strcmp(entry->address.name, "NotFound") == 0) {
      if (value_bytes != sizeof(NotFound_t))
        return DATA_BIND_ERR_TYPE_MISMATCH;
      state->error_value = ((const NotFound_t *)value)->id;
    } else if (strcmp(entry->address.name, "PermissionDenied") == 0) {
      if (value_bytes != sizeof(PermissionDenied_t))
        return DATA_BIND_ERR_TYPE_MISMATCH;
      state->error_value = ((const PermissionDenied_t *)value)->code;
    } else {
      return DATA_BIND_ERR_SCHEMA;
    }
    ++state->error_calls;
    return DATA_BIND_OK;
  }

  return DATA_BIND_ERR_SCHEMA;
}

static DataBindStatus outcome_commit(void *context, DataBindError *error) {
  OutcomeProviderState *state = (OutcomeProviderState *)context;
  (void)error;
  if (state == NULL) return DATA_BIND_ERR_INVALID_ARG;
  ++state->commit_calls;
  return DATA_BIND_OK;
}

static void outcome_abort(void *context) {
  OutcomeProviderState *state = (OutcomeProviderState *)context;
  if (state != NULL) ++state->abort_calls;
}

static DataBindBindingProvider outcome_provider(OutcomeProviderState *state) {
  DataBindBindingProvider provider =
      (DataBindBindingProvider)DATA_BIND_BINDING_PROVIDER_INIT;
  provider.context = state;
  provider.begin_output = outcome_begin;
  provider.write_output = outcome_write;
  provider.commit_output = outcome_commit;
  provider.abort_output = outcome_abort;
  return provider;
}

spec("DataBind canonical Service native lowering") {
  it("emits complete FunctionAbi and compiles the shared BindingPlan tuple") {
    const cmeta_function_desc *function =
        databind_13_ServiceNative_4_Calc_3_Add__databind_function();
    const cmeta_function_abi_desc *abi =
        databind_13_ServiceNative_4_Calc_3_Add__databind_function_abi();
    DataBindNativeTypeBinding request =
        (DataBindNativeTypeBinding){0};
    DataBindNativeTypeBinding response =
        (DataBindNativeTypeBinding){0};
    DataBindServiceNativeBinding native =
        (DataBindServiceNativeBinding){0};
    DataBindBindingProjection projection = {
        sizeof(DataBindBindingProjection),
        DATA_BIND_BINDING_PLAN_ABI_VERSION,
        "native-test",
        NULL,
        project_field,
    };
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(function);
    check_not_null(abi);
    check_true(cmeta_function_desc_valid(function));
    check_true(cmeta_function_abi_desc_valid(abi));
    check_true(abi->function == function);
    check_equal(function->name, "ServiceNative.Calc.Add");
    check_equal(function->param_count, (size_t)2u);
    check_equal(function->effects, (cmeta_effects)CMETA_EFFECT_UNKNOWN);
    check_equal(function->properties, (cmeta_properties)CMETA_PROP_NONE);
    check_equal(function->params[0].flags,
                (cmeta_param_flags)(CMETA_PARAM_IN | CMETA_PARAM_BORROWED));
    check_equal(function->params[1].flags,
                (cmeta_param_flags)(CMETA_PARAM_OUT | CMETA_PARAM_BORROWED));
    check_equal(function->params[0].type->kind, CMETA_T_POINTER);
    check_equal(function->params[1].type->kind, CMETA_T_POINTER);
    check_equal(abi->return_carrier, CMETA_ABI_SCALAR);
    check_equal(cmeta_function_param_abi(abi, 0u),
                CMETA_ABI_OBJECT_POINTER);
    check_equal(cmeta_function_param_abi(abi, 1u),
                CMETA_ABI_OBJECT_POINTER);

    check_equal(databind_13_ServiceNative_4_Calc_3_Add__databind_native_binding(
                    &request, &response, &native, &error),
                DATA_BIND_OK);
    check_equal(request.presence_count, (size_t)0u);
    check_equal(response.presence_count, (size_t)0u);
    check_null(request.presence);
    check_null(response.presence);
    check_true(native.function == function);
    check_true(native.request == &request);
    check_true(native.response == &response);

    check_equal(ServiceNative_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    check_equal(data_bind_binding_plan_compile_service(
                    codec, "Calc", "Add", &projection,
                    &native, &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    check_true(data_bind_binding_plan_function(plan) == function);
    check_equal(data_bind_binding_plan_ingress_count(plan), (size_t)2u);
    check_equal(data_bind_binding_plan_egress_count(plan), (size_t)1u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("publishes success, native status and typed errors as distinct BindingPlan outcomes") {
    const cmeta_function_desc *function =
        databind_13_ServiceNative_4_Calc_4_Find__databind_function();
    DataBindNativeTypeBinding request_binding =
        (DataBindNativeTypeBinding){0};
    DataBindNativeTypeBinding response_binding =
        (DataBindNativeTypeBinding){0};
    DataBindServiceNativeBinding native =
        (DataBindServiceNativeBinding){0};
    DataBindBindingProjection projection = {
        sizeof(DataBindBindingProjection),
        DATA_BIND_BINDING_PLAN_ABI_VERSION,
        "native-test",
        NULL,
        project_field,
    };
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    AddRequest_t request = {0};
    AddResponse_t response = {0};
    databind_13_ServiceNative_4_Calc_4_Find__error typed_error =
        databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    void *params[] = {&request, &response, &typed_error};
    const size_t param_bytes[] = {
        sizeof(request), sizeof(response), sizeof(typed_error)};
    DataBindBindingCallFrame frame =
        (DataBindBindingCallFrame)DATA_BIND_BINDING_CALL_FRAME_INIT;
    OutcomeProviderState state = {0};
    DataBindBindingProvider provider = outcome_provider(&state);
    DataBindBindingOutcome outcome =
        (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    int native_status;

    check_equal(
        databind_13_ServiceNative_4_Calc_4_Find__databind_native_binding(
            &request_binding, &response_binding, &native, &error),
        DATA_BIND_OK);
    check_true(native.function == function);
    check_equal(native.error_count, (size_t)2u);
    check_equal(native.error_param_index, (size_t)2u);
    check_equal(native.error_envelope_bytes, sizeof(typed_error));
    check_equal(native.error_kind_offset, offsetof(
        databind_13_ServiceNative_4_Calc_4_Find__error, kind));
    check_equal(native.error_kind_bytes, sizeof(uint32_t));
    check_equal(native.errors[0].kind_value, (uint32_t)1u);
    check_equal(native.errors[0].idl_type_name, "NotFound");
    check_equal(native.errors[1].kind_value, (uint32_t)2u);
    check_equal(native.errors[1].idl_type_name, "PermissionDenied");

    check_equal(ServiceNative_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    check_equal(
        data_bind_binding_plan_compile_service(
            codec, "Calc", "Find", &projection, &native, &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    check_equal(data_bind_binding_plan_error_count(plan), (size_t)2u);
    check_equal(data_bind_binding_plan_error_at(plan, 0u), "NotFound");
    check_equal(data_bind_binding_plan_error_at(plan, 1u),
                "PermissionDenied");

    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 3u;

    check_equal(
        data_bind_binding_plan_write_outputs(
            plan, &provider, &frame, &diagnostic),
        DATA_BIND_ERR_INVALID_ARG);
    check_equal(state.begin_calls, (size_t)0u);
    check(strstr(diagnostic.message, "write_outcome") != NULL);

    request.left = 7u;
    request.scale = 3u;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(native_status, 0);
    check_equal(
        data_bind_binding_plan_write_outcome(
            plan, &provider, &frame, native_status, &outcome, &diagnostic),
        DATA_BIND_OK);
    check_equal(outcome.kind, DATA_BIND_BINDING_OUTCOME_SUCCESS);
    check_equal(outcome.native_status, 0);
    check_equal(outcome.typed_error_index, SIZE_MAX);
    check_null(outcome.typed_error);
    check_equal(state.begin_calls, (size_t)1u);
    check_equal(state.write_calls, (size_t)1u);
    check_equal(state.result_calls, (size_t)1u);
    check_equal(state.error_calls, (size_t)0u);
    check_equal(state.result_value, 21u);
    check_equal(state.commit_calls, (size_t)1u);
    check_equal(state.abort_calls, (size_t)0u);

    memset(&state, 0, sizeof(state));
    request.left = 0u;
    request.scale = 41u;
    response.sum = 999u;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    outcome = (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(native_status, 0);
    check_equal(
        data_bind_binding_plan_write_outcome(
            plan, &provider, &frame, native_status, &outcome, &diagnostic),
        DATA_BIND_OK);
    check_equal(outcome.kind, DATA_BIND_BINDING_OUTCOME_TYPED_ERROR);
    check_equal(outcome.typed_error_index, (size_t)0u);
    check_equal(outcome.typed_error, "NotFound");
    check_equal(state.begin_calls, (size_t)1u);
    check_equal(state.write_calls, (size_t)1u);
    check_equal(state.result_calls, (size_t)0u);
    check_equal(state.error_calls, (size_t)1u);
    check_equal(state.error_name, "NotFound");
    check_equal(state.error_value, 41u);
    check_equal(state.commit_calls, (size_t)1u);
    check_equal(state.abort_calls, (size_t)0u);
    check_equal(response.sum, 999u);

    memset(&state, 0, sizeof(state));
    request.left = 1u;
    request.scale = 7u;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    outcome = (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(
        data_bind_binding_plan_write_outcome(
            plan, &provider, &frame, native_status, &outcome, &diagnostic),
        DATA_BIND_OK);
    check_equal(outcome.kind, DATA_BIND_BINDING_OUTCOME_TYPED_ERROR);
    check_equal(outcome.typed_error_index, (size_t)1u);
    check_equal(outcome.typed_error, "PermissionDenied");
    check_equal(state.error_calls, (size_t)1u);
    check_equal(state.error_name, "PermissionDenied");
    check_equal(state.error_value, 7u);

    memset(&state, 0, sizeof(state));
    request.left = UINT32_MAX;
    request.scale = 9u;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    outcome = (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(native_status, -9);
    check_equal(
        data_bind_binding_plan_write_outcome(
            plan, NULL, &frame, native_status, &outcome, &diagnostic),
        DATA_BIND_OK);
    check_equal(outcome.kind, DATA_BIND_BINDING_OUTCOME_NATIVE_STATUS);
    check_equal(outcome.native_status, -9);
    check_equal(state.begin_calls, (size_t)0u);
    check_equal(state.write_calls, (size_t)0u);
    check_equal(state.commit_calls, (size_t)0u);

    typed_error.kind =
        databind_13_ServiceNative_4_Calc_4_Find__ERROR_1;
    typed_error.payload.error_1.id = 77u;
    outcome = (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    check_equal(
        data_bind_binding_plan_write_outcome(
            plan, NULL, &frame, -9, &outcome, &diagnostic),
        DATA_BIND_ERR_SCHEMA);
    check_equal(outcome.kind, DATA_BIND_BINDING_OUTCOME_NONE);

    memset(&state, 0, sizeof(state));
    request.left = 0u;
    request.scale = 55u;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    state.fail_write = 1;
    check_equal(
        data_bind_binding_plan_write_outcome(
            plan, &provider, &frame, native_status, &outcome, &diagnostic),
        DATA_BIND_ERR_RUNTIME);
    check_equal(state.begin_calls, (size_t)1u);
    check_equal(state.write_calls, (size_t)1u);
    check_equal(state.commit_calls, (size_t)0u);
    check_equal(state.abort_calls, (size_t)1u);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("maps canonical BindingPlan outcomes through HTTP and RPC MethodPlans") {
    const DataBindHttpErrorMapping http_errors[] = {
        {sizeof(DataBindHttpErrorMapping), "NotFound", 404},
        {sizeof(DataBindHttpErrorMapping), "PermissionDenied", 403}};
    DataBindHttpProjectionConfig http_config =
        (DataBindHttpProjectionConfig)DATA_BIND_HTTP_PROJECTION_CONFIG_INIT;
    const DataBindRpcErrorMapping rpc_errors[] = {
        {sizeof(DataBindRpcErrorMapping), "NotFound", -32004},
        {sizeof(DataBindRpcErrorMapping), "PermissionDenied", -32003}};
    DataBindRpcProjectionConfig rpc_config =
        (DataBindRpcProjectionConfig)DATA_BIND_RPC_PROJECTION_CONFIG_INIT;
    DataBindNativeTypeBinding request_binding = {0};
    DataBindNativeTypeBinding response_binding = {0};
    DataBindServiceNativeBinding native = {0};
    DataBindHttpMethodPlan *http = NULL;
    DataBindRpcMethodPlan *rpc = NULL;
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    AddRequest_t request = {0};
    AddResponse_t response = {0};
    databind_13_ServiceNative_4_Calc_4_Find__error typed_error =
        databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    void *params[] = {&request, &response, &typed_error};
    const size_t param_bytes[] = {
        sizeof(request), sizeof(response), sizeof(typed_error)};
    DataBindBindingCallFrame frame =
        (DataBindBindingCallFrame)DATA_BIND_BINDING_CALL_FRAME_INIT;
    OutcomeProviderState state = {0};
    DataBindBindingProvider provider = outcome_provider(&state);
    DataBindBindingOutcome outcome =
        (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    DataBindHttpErrorMapping http_mapping =
        (DataBindHttpErrorMapping)DATA_BIND_HTTP_ERROR_MAPPING_INIT;
    DataBindRpcErrorMapping rpc_mapping =
        (DataBindRpcErrorMapping)DATA_BIND_RPC_ERROR_MAPPING_INIT;
    int native_status;
    int http_status = -1;
    int rpc_code = 1;

    http_config.success_status = 201;
    http_config.errors = http_errors;
    http_config.error_count = 2u;
    rpc_config.errors = rpc_errors;
    rpc_config.error_count = 2u;

    check_equal(
        databind_13_ServiceNative_4_Calc_4_Find__databind_native_binding(
            &request_binding, &response_binding, &native, &error),
        DATA_BIND_OK);
    check_equal(ServiceNative_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);

    check_equal(
        data_bind_http_method_plan_compile_service(
            codec, "Calc", "Find", &http_config, &native,
            &http, &diagnostic),
        DATA_BIND_OK);
    check_not_null(http);
    check_equal(data_bind_http_method_plan_method(http), "POST");
    check_equal(data_bind_http_method_plan_route(http), "/Calc/Find");
    check_equal(data_bind_http_method_plan_success_status(http), 201);
    check_equal(data_bind_http_method_plan_error_count(http), (size_t)2u);
    check(data_bind_http_method_plan_error_at(http, 0u, &http_mapping));
    check_equal(http_mapping.error_type, "NotFound");
    check_equal(http_mapping.status, 404);

    check_equal(
        data_bind_rpc_method_plan_compile_service(
            codec, "Calc", "Find", &rpc_config, &native,
            &rpc, &diagnostic),
        DATA_BIND_OK);
    check_not_null(rpc);
    check_equal(data_bind_rpc_method_plan_wire_method(rpc), "Calc.Find");
    check_equal(data_bind_rpc_method_plan_error_count(rpc), (size_t)2u);
    check(data_bind_rpc_method_plan_error_at(rpc, 1u, &rpc_mapping));
    check_equal(rpc_mapping.error_type, "PermissionDenied");
    check_equal(rpc_mapping.code, -32003);

    frame.request = &request;
    frame.request_bytes = sizeof(request);
    frame.params = params;
    frame.param_bytes = param_bytes;
    frame.param_count = 3u;

    request.left = 7u;
    request.scale = 3u;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(native_status, 0);
    check_equal(
        data_bind_binding_plan_write_outcome(
            data_bind_http_method_plan_binding(http), &provider, &frame,
            native_status, &outcome, &diagnostic),
        DATA_BIND_OK);
    check_equal(outcome.kind, DATA_BIND_BINDING_OUTCOME_SUCCESS);
    check(data_bind_http_method_plan_status_for_outcome(
        http, &outcome, &http_status));
    check_equal(http_status, 201);
    check(data_bind_rpc_method_plan_code_for_outcome(
        rpc, &outcome, &rpc_code));
    check_equal(rpc_code, 0);

    memset(&state, 0, sizeof(state));
    request.left = 0u;
    request.scale = 41u;
    response.sum = 999u;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    outcome = (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(
        data_bind_binding_plan_write_outcome(
            data_bind_http_method_plan_binding(http), &provider, &frame,
            native_status, &outcome, &diagnostic),
        DATA_BIND_OK);
    check_equal(outcome.kind, DATA_BIND_BINDING_OUTCOME_TYPED_ERROR);
    check_equal(outcome.typed_error, "NotFound");
    check(data_bind_http_method_plan_status_for_outcome(
        http, &outcome, &http_status));
    check_equal(http_status, 404);
    check(data_bind_rpc_method_plan_code_for_outcome(
        rpc, &outcome, &rpc_code));
    check_equal(rpc_code, -32004);

    memset(&state, 0, sizeof(state));
    request.left = 1u;
    request.scale = 7u;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    outcome = (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(
        data_bind_binding_plan_write_outcome(
            data_bind_http_method_plan_binding(http), &provider, &frame,
            native_status, &outcome, &diagnostic),
        DATA_BIND_OK);
    check_equal(outcome.typed_error, "PermissionDenied");
    check(data_bind_http_method_plan_status_for_outcome(
        http, &outcome, &http_status));
    check_equal(http_status, 403);
    check(data_bind_rpc_method_plan_code_for_outcome(
        rpc, &outcome, &rpc_code));
    check_equal(rpc_code, -32003);

    {
      DataBindBindingOutcome mismatched =
          (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
      int sentinel_http = 298;
      int sentinel_rpc = 18;
      mismatched.kind = DATA_BIND_BINDING_OUTCOME_TYPED_ERROR;
      mismatched.typed_error_index = 0u;
      mismatched.typed_error = "PermissionDenied";
      check_false(data_bind_http_method_plan_status_for_outcome(
          http, &mismatched, &sentinel_http));
      check_equal(sentinel_http, 298);
      check_false(data_bind_rpc_method_plan_code_for_outcome(
          rpc, &mismatched, &sentinel_rpc));
      check_equal(sentinel_rpc, 18);

      mismatched.typed_error_index = 99u;
      mismatched.typed_error = NULL;
      check_false(data_bind_http_method_plan_status_for_outcome(
          http, &mismatched, &sentinel_http));
      check_false(data_bind_rpc_method_plan_code_for_outcome(
          rpc, &mismatched, &sentinel_rpc));
    }

    request.left = UINT32_MAX;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    outcome = (DataBindBindingOutcome)DATA_BIND_BINDING_OUTCOME_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(
        data_bind_binding_plan_write_outcome(
            data_bind_http_method_plan_binding(http), NULL, &frame,
            native_status, &outcome, &diagnostic),
        DATA_BIND_OK);
    check_equal(outcome.kind, DATA_BIND_BINDING_OUTCOME_NATIVE_STATUS);
    http_status = 299;
    rpc_code = 17;
    check_false(data_bind_http_method_plan_status_for_outcome(
        http, &outcome, &http_status));
    check_equal(http_status, 299);
    check_false(data_bind_rpc_method_plan_code_for_outcome(
        rpc, &outcome, &rpc_code));
    check_equal(rpc_code, 17);

    data_bind_rpc_method_plan_free(rpc);
    data_bind_http_method_plan_free(http);
    data_bind_free(codec);
  }

  it("keeps native status and typed Service outcome semantically distinct") {
    AddRequest_t request = {0};
    AddResponse_t response = {0};
    databind_13_ServiceNative_4_Calc_4_Find__error typed_error =
        databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    int native_status;

    request.left = 7u;
    request.scale = 3u;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(native_status, 0);
    check_equal(
        (unsigned)typed_error.kind,
        (unsigned)databind_13_ServiceNative_4_Calc_4_Find__ERROR_NONE);
    check_equal(response.sum, 21u);

    request.left = 0u;
    request.scale = 41u;
    response.sum = 999u;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(native_status, 0);
    check_equal(
        (unsigned)typed_error.kind,
        (unsigned)databind_13_ServiceNative_4_Calc_4_Find__ERROR_1);
    check_equal(typed_error.payload.error_1.id, 41u);

    request.left = 1u;
    request.scale = 7u;
    typed_error =
        (databind_13_ServiceNative_4_Calc_4_Find__error)
            databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;
    native_status = databind_13_ServiceNative_4_Calc_4_Find(
        &request, &response, &typed_error);
    check_equal(native_status, 0);
    check_equal(
        (unsigned)typed_error.kind,
        (unsigned)databind_13_ServiceNative_4_Calc_4_Find__ERROR_2);
    check_equal(typed_error.payload.error_2.code, 7u);
  }

  it("emits deterministic typed-error envelope and third FunctionAbi parameter") {
    const cmeta_function_desc *function =
        databind_13_ServiceNative_4_Calc_4_Find__databind_function();
    const cmeta_function_abi_desc *abi =
        databind_13_ServiceNative_4_Calc_4_Find__databind_function_abi();
    databind_13_ServiceNative_4_Calc_4_Find__error error = {0};

    check_not_null(function);
    check_not_null(abi);
    check_true(cmeta_function_desc_valid(function));
    check_true(cmeta_function_abi_desc_valid(abi));
    check_true(abi->function == function);
    check_equal(function->name, "ServiceNative.Calc.Find");
    check_equal(function->param_count, (size_t)3u);

    check_equal(function->params[0].flags,
                (cmeta_param_flags)(CMETA_PARAM_IN | CMETA_PARAM_BORROWED));
    check_equal(function->params[1].flags,
                (cmeta_param_flags)(CMETA_PARAM_OUT | CMETA_PARAM_BORROWED));
    check_equal(function->params[2].flags,
                (cmeta_param_flags)(CMETA_PARAM_OUT | CMETA_PARAM_BORROWED));
    check_equal(function->params[2].type->kind, CMETA_T_POINTER);
    check_not_null(function->params[2].type->pointee);
    check_equal(
        cmeta_type_identity_of(function->params[2].type->pointee)->stable_atom_id,
        "tbe.native.ServiceNative.Calc.Find.error_t");

    check_equal(abi->return_carrier, CMETA_ABI_SCALAR);
    check_equal(cmeta_function_param_abi(abi, 0u),
                CMETA_ABI_OBJECT_POINTER);
    check_equal(cmeta_function_param_abi(abi, 1u),
                CMETA_ABI_OBJECT_POINTER);
    check_equal(cmeta_function_param_abi(abi, 2u),
                CMETA_ABI_OBJECT_POINTER);

    check_equal((unsigned)databind_13_ServiceNative_4_Calc_4_Find__ERROR_NONE,
                0u);
    check_equal((unsigned)databind_13_ServiceNative_4_Calc_4_Find__ERROR_1,
                1u);
    check_equal((unsigned)databind_13_ServiceNative_4_Calc_4_Find__ERROR_2,
                2u);

    error.kind = databind_13_ServiceNative_4_Calc_4_Find__ERROR_1;
    error.payload.error_1.id = 41u;
    check_equal(error.payload.error_1.id, 41u);

    error.kind = databind_13_ServiceNative_4_Calc_4_Find__ERROR_2;
    error.payload.error_2.code = 7u;
    check_equal(error.payload.error_2.code, 7u);
  }
}

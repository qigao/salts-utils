#include "data_bind_binding_plan.h"
#include "service_native_generated.h"
#include "tinytest.h"

#include <cmeta/function.h>
#include <string.h>

const cmeta_function_desc *
databind_13_ServiceNative_4_Calc_3_Add__databind_function(void);
const cmeta_function_abi_desc *
databind_13_ServiceNative_4_Calc_3_Add__databind_function_abi(void);
DataBindStatus databind_13_ServiceNative_4_Calc_3_Add__databind_native_binding(
    DataBindNativeTypeBinding *request_out,
    DataBindNativeTypeBinding *response_out,
    DataBindServiceNativeBinding *service_out,
    DataBindError *error);

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
}

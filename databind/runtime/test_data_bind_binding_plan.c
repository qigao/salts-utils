#include "data_bind_binding_plan.h"
#include "tinytest.h"

#include <salts_cmeta_fixed_width.h>

#include <stddef.h>
#include <stdint.h>
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
    value, int, &cmeta_type_int, calc_add,
    (const AddRequest *, request,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &ADD_REQUEST_PTR_TYPE),
    (AddResponse *, response, CMETA_PARAM_OUT, &ADD_RESPONSE_PTR_TYPE));

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
      "message CalcError { string message; }"
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

static DataBindServiceNativeBinding canonical_native(
    const cmeta_function_desc *function) {
  DataBindServiceNativeBinding native =
      DATA_BIND_SERVICE_NATIVE_BINDING_INIT(
          function, &ADD_REQUEST_NATIVE, &ADD_RESPONSE_NATIVE);
  return native;
}

spec("DataBind canonical Service BindingPlan") {
  it("compiles HTTP projection into generic logical addresses") {
    DataBind *codec = create_codec();
    DataBindServiceNativeBinding native =
        canonical_native(FunctionMeta(calc_add));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    DataBindBindingPlanEntry entry = DATA_BIND_BINDING_PLAN_ENTRY_INIT;

    check_equal(data_bind_service_binding_plan_compile(
                    codec, "Calc", "Add", "http",
                    &native, &plan, &diagnostic),
                DATA_BIND_OK);
    check_not_null(plan);
    check_equal(data_bind_binding_plan_operation_id(plan), "Calc.Add");
    check_equal(data_bind_binding_plan_projection_id(plan), "http");
    check(data_bind_binding_plan_function(plan) == FunctionMeta(calc_add));
    check_equal(data_bind_binding_plan_ingress_count(plan), 3u);
    check_equal(data_bind_binding_plan_egress_count(plan), 1u);
    check_equal(data_bind_binding_plan_error_count(plan), 1u);
    check_equal(data_bind_binding_plan_error_at(plan, 0u), "CalcError");

    check(data_bind_binding_plan_ingress_at(plan, 0u, &entry) == 1);
    check_equal(entry.schema_field, "left");
    check_true(entry.address.binding_class == DATA_BIND_BINDING_VALUE);
    check_equal(entry.address.space, "http.query");
    check_equal(entry.address.name, "left");
    check_equal(entry.function_param, "request");
    check_equal(entry.function_param_index, 0u);
    check_equal(entry.native_offset, offsetof(AddRequest, left));
    check_true(entry.parameter_indirect);

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_ingress_at(plan, 1u, &entry) == 1);
    check_true(entry.address.binding_class == DATA_BIND_BINDING_METADATA);
    check_equal(entry.address.space, "http.header");
    check_equal(entry.address.name, "X-Right");

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_ingress_at(plan, 2u, &entry) == 1);
    check_true(entry.has_default);
    check_equal(entry.default_value, "1");
    check_true(entry.has_presence);
    check_equal(entry.presence_offset, offsetof(AddRequest, presence));
    check_equal(entry.presence_bit, 0u);

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_egress_at(plan, 0u, &entry) == 1);
    check_true(entry.address.binding_class == DATA_BIND_BINDING_RESULT);
    check_equal(entry.address.space, "http.result");
    check_equal(entry.schema_field, "sum");
    check_equal(entry.function_param, "response");
    check_equal(entry.function_param_index, 1u);
    check_true(entry.parameter_indirect);
    check_false(entry.target_is_return);

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("uses the same BindingPlan ABI for RPC without a transport enum") {
    DataBind *codec = create_codec();
    DataBindServiceNativeBinding native =
        canonical_native(FunctionMeta(calc_add));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;
    DataBindBindingPlanEntry entry = DATA_BIND_BINDING_PLAN_ENTRY_INIT;

    check_equal(data_bind_service_binding_plan_compile(
                    codec, "Calc", "Add", "rpc",
                    &native, &plan, &diagnostic),
                DATA_BIND_OK);
    check(data_bind_binding_plan_ingress_at(plan, 0u, &entry) == 1);
    check_true(entry.address.binding_class == DATA_BIND_BINDING_VALUE);
    check_equal(entry.address.space, "rpc.param");
    check_equal(entry.address.name, "left");

    entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    check(data_bind_binding_plan_egress_at(plan, 0u, &entry) == 1);
    check_equal(entry.address.space, "rpc.result");

    data_bind_binding_plan_free(plan);
    data_bind_free(codec);
  }

  it("rejects a future projection cleanly without closing the ABI") {
    DataBind *codec = create_codec();
    DataBindServiceNativeBinding native =
        canonical_native(FunctionMeta(calc_add));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;

    check_equal(data_bind_service_binding_plan_compile(
                    codec, "Calc", "Add", "flowmq",
                    &native, &plan, &diagnostic),
                DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check(strstr(diagnostic.message, "not admitted") != NULL);

    data_bind_free(codec);
  }

  it("rejects ambiguous pointer-return response ownership") {
    DataBind *codec = create_codec();
    DataBindServiceNativeBinding native =
        canonical_native(FunctionMeta(calc_bad_pointer_return));
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindBindingPlan *plan = NULL;

    check_equal(data_bind_service_binding_plan_compile(
                    codec, "Calc", "Add", "http",
                    &native, &plan, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(plan);
    check(strstr(diagnostic.message, "ownership") != NULL);

    data_bind_free(codec);
  }
}

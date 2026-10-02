#include "compiler_core.h"
#include "method_plan_projection.h"
#include "binary_contract_overlay.h"

#include "salts_fs.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

#ifndef BINARY_ADMISSION_SCHEMA
#error "BINARY_ADMISSION_SCHEMA is required"
#endif

static int file_contains(const char *path, const char *needle) {
  salts_fs_buf_t buffer = {0};
  int found = 0;
  if (salts_fs_read_file(path, &buffer) == 0 && buffer.base != NULL)
    found = strstr(buffer.base, needle) != NULL;
  salts_fs_buf_free(&buffer);
  return found;
}

spec("DataBind generated Binary MethodPlan admission") {
  it("fails closed on an IDL-valid type without BinaryLayoutIR") {
    static const char output[] =
        "databind_binary_method_plan_admission.rpc.h";
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    databind_binary_format_plan format_plan = {0};
    tbe_error_t format_error;
    char *schema_data = NULL;
    databind_compiler_rpc_operation_config operation = {
        "BinaryGate", "Use", "binary.use",
        DATA_BIND_FORMAT_JSON, DATA_BIND_FORMAT_JSON};
    databind_compiler_rpc_projection_config config = {
        "binary_admission",
        &operation, 1u,
        NULL, 0u,
        NULL, 0u};
    databind_compiler_projection_request request = {
        {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
         DATABIND_COMPILER_TRANSPORT_RPC},
        output,
        &config};
    databind_compiler_projection_backend backend =
        databind_compiler_rpc_method_plan_backend();

    (void)salts_fs_unlink(output);
    check_equal(
        databind_compiler_parse_contract_file(
            BINARY_ADMISSION_SCHEMA, &root, &contract, &schema_data),
        0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || contract == NULL || schema_data == NULL) {
      idl_contract_destroy(contract);
      node_free(root);
      free(schema_data);
      return;
    }
    tbe_error_init(&format_error);
    check(databind_binary_format_plan_build(
        contract, root, &format_plan, &format_error));
    input = (databind_compiler_projection_input){
        .contract = contract,
        .tbe_format = &format_plan};

    /* Non-Binary representation does not consult BinaryLayoutIR. */
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        0);
    check_equal(
        salts_fs_access(output, SALTS_FS_ACCESS_EXISTS),
        0);
    (void)salts_fs_unlink(output);

    /*
     * The same canonical Service contract is legal, but Choice has no
     * BinaryLayoutIR. Binary ingress must reject before publishing output.
     */
    operation.ingress_format = DATA_BIND_FORMAT_BINARY;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    {
      static const char opaque_output[] =
          "databind_opaque_http_admission.http.h";
      databind_compiler_http_operation_config http_operation = {
          .service_name = "Raw",
          .operation_name = "Echo",
          .method = "POST",
          .route = "/raw",
          .success_status = 200,
          .context_flags = 0u,
          .ingress_format = DATA_BIND_FORMAT_NONE,
          .egress_format = DATA_BIND_FORMAT_NONE,
          .ingress_payload_kind = DATA_BIND_PAYLOAD_OPAQUE,
          .egress_payload_kind = DATA_BIND_PAYLOAD_OPAQUE,
          .opaque_max_bytes = 64u};
      databind_compiler_http_projection_config http_config = {
          .symbol_prefix = "opaque_admission",
          .operations = &http_operation,
          .operation_count = 1u};
      databind_compiler_projection_request http_request = {
          {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
           DATABIND_COMPILER_TRANSPORT_HTTP},
          opaque_output,
          &http_config};
      databind_compiler_projection_backend http_backend =
          databind_compiler_http_method_plan_backend();

      (void)salts_fs_unlink(opaque_output);
      check_equal(
          databind_compiler_projection_run(
              &input, &http_request, 1u, &http_backend, 1u),
          0);
      check(file_contains(opaque_output, "DATA_BIND_FORMAT_NONE"));
      check(file_contains(opaque_output, "DATA_BIND_PAYLOAD_OPAQUE"));
      check(file_contains(
          opaque_output, "DATA_BIND_OPAQUE_STATE_VALUE, 64u"));
      check(file_contains(
          opaque_output, "&opaque_admission_opaque_plan"));
      (void)salts_fs_unlink(opaque_output);

      http_operation.service_name = "BinaryGate";
      http_operation.operation_name = "Use";
      http_operation.route = "/bad";
      check_equal(
          databind_compiler_projection_run(
              &input, &http_request, 1u, &http_backend, 1u),
          -1);
      check(salts_fs_access(opaque_output, SALTS_FS_ACCESS_EXISTS) != 0);
    }

    databind_binary_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }
}

#include "compiler_core.h"
#include "method_plan_projection.h"

#include "salts_fs.h"
#include "tinytest.h"

#include <stdlib.h>

#ifndef BINARY_ADMISSION_SCHEMA
#error "BINARY_ADMISSION_SCHEMA is required"
#endif

spec("DataBind generated Binary MethodPlan admission") {
  it("fails closed on an IDL-valid type without BinaryLayoutIR") {
    static const char output[] =
        "databind_binary_method_plan_admission.rpc.h";
    Node *root = NULL;
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
        tbe_compiler_parse_schema_file(
            BINARY_ADMISSION_SCHEMA, &root, &schema_data),
        0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || schema_data == NULL) {
      node_free(root);
      free(schema_data);
      return;
    }

    /* Non-Binary representation does not consult BinaryLayoutIR. */
    check_equal(
        databind_compiler_projection_run(
            root, &request, 1u, &backend, 1u),
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
            root, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    node_free(root);
    free(schema_data);
  }
}

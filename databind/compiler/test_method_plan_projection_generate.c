#include "compiler_core.h"
#include "method_plan_projection.h"

#include <stdlib.h>

int main(int argc, char **argv) {
  Node *root = NULL;
  char *schema_data = NULL;
  databind_compiler_http_operation_config http_operations[] = {
      {"Calc", "Add", "GET", "/add/{left}", 201,
       UINT64_C(4), DATABIND_COMPILER_FORMAT_YAML,
       DATABIND_COMPILER_FORMAT_XML}
  };
  databind_compiler_http_field_config http_fields[] = {
      {"Calc", "Add", DATABIND_COMPILER_PROJECTION_INGRESS,
       "left", DATABIND_COMPILER_HTTP_PATH, "left", SIZE_MAX},
      {"Calc", "Add", DATABIND_COMPILER_PROJECTION_INGRESS,
       "right", DATABIND_COMPILER_HTTP_HEADER, "X-Right", SIZE_MAX},
      {"Calc", "Add", DATABIND_COMPILER_PROJECTION_EGRESS,
       "sum", DATABIND_COMPILER_HTTP_RESPONSE_BODY, "sum", SIZE_MAX}
  };
  databind_compiler_http_error_config http_errors[] = {
      {"Calc", "Add", "CalcError", 422}
  };
  databind_compiler_http_projection_config http_config = {
      "projection_fixture",
      http_operations, sizeof(http_operations) / sizeof(http_operations[0]),
      http_fields, sizeof(http_fields) / sizeof(http_fields[0]),
      http_errors, sizeof(http_errors) / sizeof(http_errors[0])
  };
  databind_compiler_rpc_operation_config rpc_operations[] = {
      {"Calc", "Add", "calc.add",
       DATABIND_COMPILER_FORMAT_JSON,
       DATABIND_COMPILER_FORMAT_BINARY}
  };
  databind_compiler_rpc_field_config rpc_fields[] = {
      {"Calc", "Add", DATABIND_COMPILER_PROJECTION_INGRESS,
       "left", "lhs", 0u},
      {"Calc", "Add", DATABIND_COMPILER_PROJECTION_INGRESS,
       "right", "rhs", 1u},
      {"Calc", "Add", DATABIND_COMPILER_PROJECTION_EGRESS,
       "sum", "result", 0u}
  };
  databind_compiler_rpc_error_config rpc_errors[] = {
      {"Calc", "Add", "CalcError", -32042}
  };
  databind_compiler_rpc_projection_config rpc_config = {
      "projection_fixture",
      rpc_operations, sizeof(rpc_operations) / sizeof(rpc_operations[0]),
      rpc_fields, sizeof(rpc_fields) / sizeof(rpc_fields[0]),
      rpc_errors, sizeof(rpc_errors) / sizeof(rpc_errors[0])
  };
  databind_compiler_projection_request requests[2];
  databind_compiler_projection_backend backends[2];
  int result;

  if (argc != 4) return 2;
  if (tbe_compiler_parse_schema_file(argv[1], &root, &schema_data) != 0)
    return 3;

  requests[0] = (databind_compiler_projection_request){
      DATABIND_COMPILER_PROJECTION_HTTP, argv[2], &http_config};
  requests[1] = (databind_compiler_projection_request){
      DATABIND_COMPILER_PROJECTION_RPC, argv[3], &rpc_config};
  backends[0] = databind_compiler_http_method_plan_backend();
  backends[1] = databind_compiler_rpc_method_plan_backend();

  result = databind_compiler_projection_run(
      root, requests, 2u, backends, 2u);

  node_free(root);
  free(schema_data);
  return result == 0 ? 0 : 4;
}

#include "compiler_core.h"
#include "plugin_projection.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  Node *root = NULL;
  char *schema_data = NULL;
  databind_compiler_plugin_config config;
  databind_compiler_projection_request request;
  databind_compiler_projection_backend backend =
      DATABIND_COMPILER_PLUGIN_BACKEND;
  int status;

  if (argc != 6) {
    fprintf(stderr,
            "usage: %s <schema> <plugin.c> <service.h> <native.h> <component>\n",
            argc > 0 ? argv[0] : "plugin-codegen");
    return 2;
  }

  status = tbe_compiler_parse_schema_file(argv[1], &root, &schema_data);
  if (status != 0) return status;

  config = (databind_compiler_plugin_config){
      .plugin_version_major = 1u,
      .plugin_version_minor = 0u,
      .plugin_version_patch = 0u,
      .component_id = argv[5],
      .native_header = argv[4],
      .service_header_output = argv[3],
  };
  request = (databind_compiler_projection_request){
      .kind = DATABIND_COMPILER_PROJECTION_PLUGIN,
      .output = argv[2],
      .config = &config,
  };

  status = databind_compiler_projection_run(
      root, &request, 1u, &backend, 1u);

  free(schema_data);
  node_free(root);
  return status == 0 ? 0 : 1;
}

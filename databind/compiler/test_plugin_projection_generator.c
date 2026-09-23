#include "compiler_core.h"
#include "plugin_projection.h"
#include "projection.h"

#include <stdlib.h>

int main(int argc, char **argv) {
  tbe_compiler_options_t native_options;
  databind_compiler_plugin_config plugin_config;
  databind_compiler_projection_backend backend;
  databind_compiler_projection_request request;
  Node *root = NULL;
  char *schema_data = NULL;
  int status;

  if (argc != 5) return 2;

  native_options = (tbe_compiler_options_t){0};
  native_options.schema_path = argv[1];
  native_options.output_path = argv[2];
  native_options.source_output_path = argv[3];
  native_options.resource_dir = TBE_COMPILER_RESOURCE_DIR;
  native_options.lang_enum = TBE_COMPILER_LANG_C;

  if (tbe_compiler_run(&native_options) != 0) return 3;

  status = tbe_compiler_parse_schema_file(
      argv[1], &root, &schema_data);
  if (status != 0) return 4;

  plugin_config = (databind_compiler_plugin_config){
      "image.generated.h", 4u, 0u, 0u};
  backend = databind_compiler_plugin_backend(&plugin_config);
  request = (databind_compiler_projection_request){
      DATABIND_COMPILER_PROJECTION_PLUGIN, argv[4], NULL};

  status = databind_compiler_projection_run(
      root, &request, 1u, &backend, 1u);

  node_free(root);
  free(schema_data);
  return status == 0 ? 0 : 5;
}

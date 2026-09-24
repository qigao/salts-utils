#include "compiler_core.h"
#include "service_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_header(
    const char *path,
    const char *native_header,
    const databind_compiler_service_native_ir *ir) {
  FILE *file = fopen(path, "wb");
  size_t i;
  if (file == NULL) return 0;
  if (fprintf(
          file,
          "#ifndef SERVICE_NATIVE_GENERATED_H\n"
          "#define SERVICE_NATIVE_GENERATED_H\n\n"
          "#include \"%s\"\n\n"
          "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n",
          native_header) < 0)
    goto fail;
  for (i = 0u; i < ir->operation_count; ++i)
    if (databind_compiler_service_native_emit_prototype(
            file, &ir->operations[i]) != 0)
      goto fail;
  if (fputs(
          "\n#ifdef __cplusplus\n}\n#endif\n\n"
          "#endif /* SERVICE_NATIVE_GENERATED_H */\n",
          file) == EOF)
    goto fail;
  return fclose(file) == 0;
fail:
  fclose(file);
  return 0;
}

static int write_source(
    const char *path,
    const char *header_name,
    const databind_compiler_service_native_ir *ir) {
  FILE *file = fopen(path, "wb");
  size_t i;
  if (file == NULL) return 0;
  if (fprintf(
          file,
          "#include \"%s\"\n"
          "#include \"data_bind_binding_plan.h\"\n"
          "#include <cmeta/function.h>\n"
          "#include <stddef.h>\n\n",
          header_name) < 0)
    goto fail;
  for (i = 0u; i < ir->operation_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, &ir->operations[i]) != 0 ||
        databind_compiler_service_native_emit_binding(
            file, &ir->operations[i]) != 0)
      goto fail;
    if (fputc('\n', file) == EOF) goto fail;
  }
  return fclose(file) == 0;
fail:
  fclose(file);
  return 0;
}

int main(int argc, char **argv) {
  Node *root = NULL;
  char *schema_data = NULL;
  databind_compiler_service_native_ir ir = {0};
  int status = 1;

  if (argc != 5) {
    fprintf(stderr,
            "usage: %s <schema> <service.h> <service.c> <native-header>\n",
            argc > 0 ? argv[0] : "service-native-codegen");
    return 2;
  }

  if (tbe_compiler_parse_schema_file(argv[1], &root, &schema_data) != 0)
    goto cleanup;
  if (databind_compiler_service_native_build(root, &ir) != 0)
    goto cleanup;
  if (ir.operation_count != 3u ||
      strcmp(ir.operations[0].symbol,
             "databind_13_ServiceNative_4_Calc_3_Add") != 0 ||
      strcmp(ir.operations[1].symbol,
             "databind_13_ServiceNative_3_A_B_1_C") != 0 ||
      strcmp(ir.operations[2].symbol,
             "databind_13_ServiceNative_1_A_3_B_C") != 0 ||
      strcmp(ir.operations[1].symbol, ir.operations[2].symbol) == 0)
    goto cleanup;
  if (!write_header(argv[2], argv[4], &ir) ||
      !write_source(argv[3], argv[2], &ir))
    goto cleanup;

  status = 0;

cleanup:
  databind_compiler_service_native_destroy(&ir);
  node_free(root);
  free(schema_data);
  return status;
}

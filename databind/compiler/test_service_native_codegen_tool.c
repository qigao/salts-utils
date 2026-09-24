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
            file, &ir->operations[i], 1) != 0)
      goto fail;
    if (ir->operations[i].error_count == 0u &&
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

  if (argc != 7) {
    fprintf(stderr,
            "usage: %s <schema> <service.h> <service.c> <native-header> <reject-schema> <owned-error-reject-schema>\n",
            argc > 0 ? argv[0] : "service-native-codegen");
    return 2;
  }

  if (tbe_compiler_parse_schema_file(argv[1], &root, &schema_data) != 0) {
    fprintf(stderr, "service-native-codegen: failed to parse main schema\n");
    goto cleanup;
  }
  if (databind_compiler_service_native_build(root, &ir) != 0) {
    fprintf(stderr, "service-native-codegen: failed to build main native IR\n");
    goto cleanup;
  }
  if (ir.operation_count != 4u ||
      strcmp(ir.operations[0].symbol,
             "databind_13_ServiceNative_4_Calc_3_Add") != 0 ||
      strcmp(ir.operations[1].symbol,
             "databind_13_ServiceNative_4_Calc_4_Find") != 0 ||
      ir.operations[1].error_count != 2u ||
      strcmp(ir.operations[1].errors[0].type_name, "NotFound") != 0 ||
      ir.operations[1].errors[0].kind_value != 1u ||
      strcmp(ir.operations[1].errors[1].type_name, "PermissionDenied") != 0 ||
      ir.operations[1].errors[1].kind_value != 2u ||
      strcmp(ir.operations[2].symbol,
             "databind_13_ServiceNative_3_A_B_1_C") != 0 ||
      strcmp(ir.operations[3].symbol,
             "databind_13_ServiceNative_1_A_3_B_C") != 0 ||
      strcmp(ir.operations[2].symbol, ir.operations[3].symbol) == 0) {
    fprintf(stderr, "service-native-codegen: unexpected main native IR\n");
    goto cleanup;
  }
  if (!write_header(argv[2], argv[4], &ir)) {
    fprintf(stderr, "service-native-codegen: failed to emit service header\n");
    goto cleanup;
  }
  if (!write_source(argv[3], argv[2], &ir)) {
    fprintf(stderr, "service-native-codegen: failed to emit service source\n");
    goto cleanup;
  }

  {
    int reject_index;
    for (reject_index = 5; reject_index <= 6; ++reject_index) {
      Node *reject_root = NULL;
      char *reject_schema_data = NULL;
      databind_compiler_service_native_ir reject_ir = {0};

      if (tbe_compiler_parse_schema_file(
              argv[reject_index],
              &reject_root, &reject_schema_data) != 0) {
        fprintf(stderr,
                "service-native-codegen: failed to parse reject schema %s\n",
                argv[reject_index]);
        node_free(reject_root);
        free(reject_schema_data);
        goto cleanup;
      }
      if (databind_compiler_service_native_build(
              reject_root, &reject_ir) == 0) {
        fprintf(stderr,
                "service-native-codegen: reject schema unexpectedly lowered: %s\n",
                argv[reject_index]);
        databind_compiler_service_native_destroy(&reject_ir);
        node_free(reject_root);
        free(reject_schema_data);
        goto cleanup;
      }
      databind_compiler_service_native_destroy(&reject_ir);
      node_free(reject_root);
      free(reject_schema_data);
    }
  }

  status = 0;

cleanup:
  databind_compiler_service_native_destroy(&ir);
  node_free(root);
  free(schema_data);
  return status;
}

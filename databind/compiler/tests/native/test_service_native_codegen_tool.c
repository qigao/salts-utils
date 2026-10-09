#include "compiler_core.h"
#include "service_native.h"
#include "native_service_projection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int generated_source_has_raii_cleanup(const char *path) {
  static const char expected[] =
      "native_status != 0)\n"
      "    goto cleanup;\n"
      "  return true;\n"
      "cleanup:\n"
      "  (void)cmeta_data_value_restore_zero(response_data, out);\n"
      "  return false;\n";
  FILE *file = NULL;
  char *text = NULL;
  long end;
  size_t size;
  int ok = 0;

  if (path == NULL) return 0;
  file = fopen(path, "rb");
  if (file == NULL) return 0;
  if (fseek(file, 0, SEEK_END) != 0) goto cleanup;
  end = ftell(file);
  if (end < 0 || fseek(file, 0, SEEK_SET) != 0) goto cleanup;
  size = (size_t)end;
  text = (char *)malloc(size + 1u);
  if (text == NULL) goto cleanup;
  if (fread(text, 1u, size, file) != size) goto cleanup;
  text[size] = '\0';
  ok = strstr(text, expected) != NULL;

cleanup:
  free(text);
  fclose(file);
  return ok;
}

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
          "#include <cflow/function_projection.h>\n"
          "#include <stddef.h>\n"
          "#include <string.h>\n\n",
          header_name) < 0)
    goto fail;
  for (i = 0u; i < ir->operation_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, &ir->operations[i], 1) != 0)
      goto fail;
    if (databind_compiler_service_native_emit_execution(
            file, &ir->operations[i], 1) != 0)
      goto fail;
    if (databind_compiler_service_native_emit_binding(
            file, &ir->operations[i]) != 0)
      goto fail;
    if (databind_compiler_service_native_emit_cflow_projection(
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
  IdlContract *contract = NULL;
  char *schema_data = NULL;
  databind_compiler_service_native_ir ir = {0};
  int status = 1;

  if (argc != 7) {
    fprintf(stderr,
            "usage: %s <schema> <service.h> <service.c> <native-header> <overlay-schema> <owned-error-reject-schema>\n",
            argc > 0 ? argv[0] : "service-native-codegen");
    return 2;
  }

  if (databind_compiler_parse_contract_file(
          argv[1], &root, &contract, &schema_data) != 0) {
    fprintf(stderr, "service-native-codegen: failed to parse main schema\n");
    goto cleanup;
  }
  /* Stage-only generation must leave coordinator-owned final paths alone and
   * retain final header names in generated source. */
  {
    char header_stage[4096];
    char source_stage[4096];
    databind_compiler_native_service_config config = {
        .native_header = argv[4], .header_output = argv[2],
        .binary_presentation = 1};
    databind_compiler_projection_input input = {.contract = contract};
    databind_compiler_projection_request request = {
        .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
               DATABIND_COMPILER_ARTIFACT_NATIVE},
        .output = argv[3], .config = &config};
    if (snprintf(header_stage, sizeof(header_stage), "%s.stage-smoke", argv[2]) < 0 ||
        strlen(argv[2]) + sizeof(".stage-smoke") > sizeof(header_stage) ||
        snprintf(source_stage, sizeof(source_stage), "%s.stage-smoke", argv[3]) < 0 ||
        strlen(argv[3]) + sizeof(".stage-smoke") > sizeof(source_stage))
      goto cleanup;
    (void)remove(header_stage);
    (void)remove(source_stage);
    /* Mirror the compiler coordinator's zero-byte path reservations. */
    {
      FILE *reserved_header = fopen(header_stage, "wb");
      FILE *reserved_source = fopen(source_stage, "wb");
      if (reserved_header == NULL || reserved_source == NULL) {
        if (reserved_header != NULL) (void)fclose(reserved_header);
        if (reserved_source != NULL) (void)fclose(reserved_source);
        goto cleanup;
      }
      if (fclose(reserved_header) != 0 || fclose(reserved_source) != 0)
        goto cleanup;
    }
    if (databind_compiler_native_service_render_staged(
            &input, &request, header_stage, source_stage) != 0 ||
        !generated_source_has_raii_cleanup(source_stage)) {
      fprintf(stderr, "service-native-codegen: staged render failed\n");
      (void)remove(header_stage);
      (void)remove(source_stage);
      goto cleanup;
    }
    /* Existing coordinator-owned staging files must not be overwritten. */
    if (databind_compiler_native_service_render_staged(
            &input, &request, header_stage, source_stage) == 0) {
      fprintf(stderr, "service-native-codegen: accepted occupied staging path\n");
      (void)remove(header_stage);
      (void)remove(source_stage);
      goto cleanup;
    }
    /* Staging must preserve the final header basename, not the stage name. */
    {
      FILE *staged = fopen(source_stage, "rb");
      char include_line[4096] = {0};
      const char *final_name = strrchr(argv[2], '/');
      const char *windows_name = strrchr(argv[2], '\\');
      if (windows_name != NULL &&
          (final_name == NULL || windows_name > final_name))
        final_name = windows_name;
      final_name = final_name != NULL ? final_name + 1 : argv[2];
      if (staged == NULL ||
          fgets(include_line, sizeof(include_line), staged) == NULL ||
          strstr(include_line, final_name) == NULL ||
          strstr(include_line, ".stage-smoke") != NULL) {
        fprintf(stderr, "service-native-codegen: staging path leaked into include\n");
        if (staged != NULL) (void)fclose(staged);
        (void)remove(header_stage);
        (void)remove(source_stage);
        goto cleanup;
      }
      if (fclose(staged) != 0) goto cleanup;
    }
    (void)remove(header_stage);
    (void)remove(source_stage);
  }
  if (databind_compiler_service_native_build(contract, &ir) != 0) {
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
  if (!generated_source_has_raii_cleanup(argv[3])) {
    fprintf(stderr,
            "service-native-codegen: generated CFlow cleanup plan missing\n");
    goto cleanup;
  }

  {
    Node *overlay_root = NULL;
    IdlContract *overlay_contract = NULL;
    char *overlay_schema_data = NULL;
    databind_compiler_service_native_ir overlay_ir = {0};
    const databind_compiler_service_native_operation *operation;

    if (databind_compiler_parse_contract_file(
            argv[5], &overlay_root, &overlay_contract,
            &overlay_schema_data) != 0) {
      fprintf(stderr,
              "service-native-codegen: failed to parse overlay schema %s\n",
              argv[5]);
      idl_contract_destroy(overlay_contract);
      node_free(overlay_root);
      free(overlay_schema_data);
      goto cleanup;
    }
    if (databind_compiler_service_native_build(
            overlay_contract, &overlay_ir) != 0 ||
        overlay_ir.operation_count != 1u) {
      fprintf(stderr,
              "service-native-codegen: overlay schema failed to lower\n");
      databind_compiler_service_native_destroy(&overlay_ir);
      idl_contract_destroy(overlay_contract);
      node_free(overlay_root);
      free(overlay_schema_data);
      goto cleanup;
    }

    operation = &overlay_ir.operations[0];
    if (operation->request_presence_count != 2u ||
        operation->request_null_count != 2u ||
        operation->response_presence_count != 1u ||
        operation->response_null_count != 1u ||
        operation->request_presence == NULL ||
        operation->request_nulls == NULL ||
        operation->response_presence == NULL ||
        operation->response_nulls == NULL ||
        strcmp(operation->request_presence[0].field_name, "optional_id") != 0 ||
        operation->request_presence[0].bit != 0u ||
        strcmp(operation->request_presence[1].field_name, "tri_id") != 0 ||
        operation->request_presence[1].bit != 1u ||
        strcmp(operation->request_nulls[0].field_name, "nullable_id") != 0 ||
        operation->request_nulls[0].bit != 0u ||
        strcmp(operation->request_nulls[1].field_name, "tri_id") != 0 ||
        operation->request_nulls[1].bit != 1u ||
        strcmp(operation->response_presence[0].field_name, "value") != 0 ||
        operation->response_presence[0].bit != 0u ||
        strcmp(operation->response_nulls[0].field_name, "value") != 0 ||
        operation->response_nulls[0].bit != 0u) {
      fprintf(stderr,
              "service-native-codegen: unexpected overlay state metadata\n");
      databind_compiler_service_native_destroy(&overlay_ir);
      idl_contract_destroy(overlay_contract);
      node_free(overlay_root);
      free(overlay_schema_data);
      goto cleanup;
    }

    {
      FILE *projection = tmpfile();
      char buffer[4096];
      size_t bytes;
      if (projection == NULL ||
          databind_compiler_service_native_emit_cflow_projection(
              projection, operation) != 0 ||
          fflush(projection) != 0 ||
          fseek(projection, 0, SEEK_SET) != 0) {
        if (projection != NULL) fclose(projection);
        fprintf(stderr,
                "service-native-codegen: overlay CFlow projection emit failed\n");
        databind_compiler_service_native_destroy(&overlay_ir);
        idl_contract_destroy(overlay_contract);
        node_free(overlay_root);
        free(overlay_schema_data);
        goto cleanup;
      }
      bytes = fread(buffer, 1u, sizeof(buffer) - 1u, projection);
      buffer[bytes] = '\0';
      fclose(projection);
      if (strstr(buffer, "CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE") == NULL ||
          strstr(buffer, "__databind_cflow_invoke") != NULL) {
        fprintf(stderr,
                "service-native-codegen: overlay CFlow projection did not fail closed\n");
        databind_compiler_service_native_destroy(&overlay_ir);
        idl_contract_destroy(overlay_contract);
        node_free(overlay_root);
        free(overlay_schema_data);
        goto cleanup;
      }
    }

    databind_compiler_service_native_destroy(&overlay_ir);
    idl_contract_destroy(overlay_contract);
    node_free(overlay_root);
    free(overlay_schema_data);
  }

  {
    Node *reject_root = NULL;
    IdlContract *reject_contract = NULL;
    char *reject_schema_data = NULL;
    databind_compiler_service_native_ir reject_ir = {0};

    if (databind_compiler_parse_contract_file(
            argv[6], &reject_root, &reject_contract,
            &reject_schema_data) != 0) {
      fprintf(stderr,
              "service-native-codegen: failed to parse reject schema %s\n",
              argv[6]);
      idl_contract_destroy(reject_contract);
      node_free(reject_root);
      free(reject_schema_data);
      goto cleanup;
    }
    if (databind_compiler_service_native_build(
            reject_contract, &reject_ir) == 0) {
      fprintf(stderr,
              "service-native-codegen: reject schema unexpectedly lowered: %s\n",
              argv[6]);
      databind_compiler_service_native_destroy(&reject_ir);
      idl_contract_destroy(reject_contract);
      node_free(reject_root);
      free(reject_schema_data);
      goto cleanup;
    }
    databind_compiler_service_native_destroy(&reject_ir);
    idl_contract_destroy(reject_contract);
    node_free(reject_root);
    free(reject_schema_data);
  }

  status = 0;

cleanup:
  databind_compiler_service_native_destroy(&ir);
  idl_contract_destroy(contract);
  node_free(root);
  free(schema_data);
  return status;
}

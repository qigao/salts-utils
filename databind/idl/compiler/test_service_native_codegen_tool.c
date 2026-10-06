#include "compiler_core.h"
#include "service_native.h"

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

static char *read_stream_text(FILE *file) {
  char *text = NULL;
  long end;
  size_t size;
  if (file == NULL || fflush(file) != 0 ||
      fseek(file, 0, SEEK_END) != 0)
    return NULL;
  end = ftell(file);
  if (end < 0 || fseek(file, 0, SEEK_SET) != 0)
    return NULL;
  size = (size_t)end;
  text = (char *)malloc(size + 1u);
  if (text == NULL) return NULL;
  if (fread(text, 1u, size, file) != size) {
    free(text);
    return NULL;
  }
  text[size] = '\0';
  return text;
}

static size_t text_count(const char *text, const char *needle) {
  size_t count = 0u;
  size_t length;
  if (text == NULL || needle == NULL || needle[0] == '\0') return 0u;
  length = strlen(needle);
  while ((text = strstr(text, needle)) != NULL) {
    ++count;
    text += length;
  }
  return count;
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

  if (argc != 8) {
    fprintf(stderr,
            "usage: %s <schema> <service.h> <service.c> <native-header> <overlay-schema> <owned-error-reject-schema> <cleanup-error-schema>\n",
            argc > 0 ? argv[0] : "service-native-codegen");
    return 2;
  }

  if (databind_compiler_parse_contract_file(
          argv[1], &root, &contract, &schema_data) != 0) {
    fprintf(stderr, "service-native-codegen: failed to parse main schema\n");
    goto cleanup;
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
    Node *cleanup_root = NULL;
    IdlContract *cleanup_contract = NULL;
    char *cleanup_schema_data = NULL;
    databind_compiler_service_native_ir cleanup_ir = {0};
    FILE *generated = NULL;
    char *text = NULL;
    char *init = NULL;
    char *clear = NULL;
    char *move = NULL;
    char *clear_bytes = NULL;
    char *clear_string = NULL;

    if (databind_compiler_parse_contract_file(
            argv[7], &cleanup_root, &cleanup_contract,
            &cleanup_schema_data) != 0 ||
        databind_compiler_service_native_build(
            cleanup_contract, &cleanup_ir) != 0 ||
        cleanup_ir.operation_count != 1u ||
        cleanup_ir.operations[0].error_count != 1u ||
        cleanup_ir.operations[0].errors[0].field_count != 2u) {
      fprintf(stderr,
              "service-native-codegen: managed cleanup schema failed to lower\n");
      goto cleanup_policy_done;
    }

    generated = tmpfile();
    if (generated == NULL ||
        databind_compiler_service_native_emit_reflection(
            generated, &cleanup_ir.operations[0], 1) != 0 ||
        (text = read_stream_text(generated)) == NULL) {
      fprintf(stderr,
              "service-native-codegen: managed cleanup source emit failed\n");
      goto cleanup_policy_done;
    }

    init = strstr(text, "__error_1_init(");
    clear = strstr(text, "__error_1_clear(");
    move = clear != NULL ? strstr(clear, "__error_1_move(") : NULL;
    if (init == NULL || clear == NULL || move == NULL || init >= clear ||
        strstr(init, "DataBindStatus cleanup_status = DATA_BIND_OK;") == NULL ||
        strstr(init, "cleanup_status = DATA_BIND_ERR_RUNTIME;") == NULL ||
        strstr(init, "if (cleanup_status == DATA_BIND_OK)\n"
                     "      memset(payload, 0, sizeof(*payload));") == NULL) {
      fprintf(stderr,
              "service-native-codegen: init rollback cleanup policy missing\n");
      goto cleanup_policy_done;
    }

    if (text_count(move, "DataBindStatus rollback_status = DATA_BIND_OK;") != 2u ||
        strstr(move, "(void)cmeta_data_value_move(") != NULL ||
        strstr(move, "rollback_status = DATA_BIND_ERR_RUNTIME;") == NULL ||
        strstr(text, "error->kind = kind;\n"
                     "    status = ") == NULL ||
        strstr(text, "destination->kind = kind;\n"
                     "    status = ") == NULL ||
        strstr(text, "__error_init(error);\n"
                     "    return status;") != NULL ||
        strstr(text, "__error_init(destination);\n"
                     "    return status;") != NULL) {
      fprintf(stderr,
              "service-native-codegen: move rollback carrier policy missing\n");
      goto cleanup_policy_done;
    }

    *move = '\0';
    clear_bytes = strstr(
        clear,
        "cmeta_data_value_restore_zero(&stl_byte_buffer_cmeta_data, "
        "&payload->payload)");
    clear_string = strstr(
        clear,
        "cmeta_data_value_restore_zero(&salts_tstr_cmeta_data, "
        "&payload->detail)");
    if (clear_bytes == NULL || clear_string == NULL ||
        clear_bytes >= clear_string ||
        strstr(clear, "DataBindStatus status = DATA_BIND_OK;") == NULL ||
        text_count(clear, "cmeta_data_value_restore_zero(") != 2u ||
        text_count(clear, "status = DATA_BIND_ERR_RUNTIME;") != 2u ||
        strstr(clear, "if (status == DATA_BIND_OK)\n"
                      "    memset(payload, 0, sizeof(*payload));") == NULL ||
        strstr(clear, "return DATA_BIND_ERR_RUNTIME;") != NULL) {
      fprintf(stderr,
              "service-native-codegen: clear cleanup failure policy missing\n");
      goto cleanup_policy_done;
    }

    free(text);
    text = NULL;
    fclose(generated);
    generated = NULL;
    databind_compiler_service_native_destroy(&cleanup_ir);
    idl_contract_destroy(cleanup_contract);
    node_free(cleanup_root);
    free(cleanup_schema_data);
    cleanup_contract = NULL;
    cleanup_root = NULL;
    cleanup_schema_data = NULL;
    goto cleanup_policy_pass;

cleanup_policy_done:
    free(text);
    if (generated != NULL) fclose(generated);
    databind_compiler_service_native_destroy(&cleanup_ir);
    idl_contract_destroy(cleanup_contract);
    node_free(cleanup_root);
    free(cleanup_schema_data);
    goto cleanup;

cleanup_policy_pass:
    ;
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

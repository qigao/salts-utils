#include "native_service_projection.h"

#include "service_native.h"
#include "native_source_ir.h"
#include "cmeta_fs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct native_service_output {
  const char *final_path;
  char *staging_path;
  char *backup_path;
  int had_original;
  int published;
} native_service_output;

static int native_service_text_valid(const char *text) {
  return text != NULL && text[0] != '\0';
}

static const char *native_service_basename(const char *path) {
  const char *base = path;
  const char *p;
  if (path == NULL) return NULL;
  for (p = path; *p != '\0'; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;
  return base;
}

static char *native_service_suffixed_path(
    const char *path, const char *suffix) {
  size_t a;
  size_t b;
  char *out;
  if (path == NULL || suffix == NULL) return NULL;
  a = strlen(path);
  b = strlen(suffix);
  if (a > SIZE_MAX - b - 1u) return NULL;
  out = (char *)malloc(a + b + 1u);
  if (out == NULL) return NULL;
  memcpy(out, path, a);
  memcpy(out + a, suffix, b + 1u);
  return out;
}

static void native_service_unlink_if_exists(const char *path) {
  if (path != NULL &&
      cmeta_fs_access(path, SALTS_FS_ACCESS_EXISTS) == 0)
    (void)cmeta_fs_unlink(path);
}

static FILE *native_service_open_staging(
    const char *final_path, char **out_staging) {
  FILE *file;
  char *staging;
  if (out_staging == NULL || !native_service_text_valid(final_path))
    return NULL;
  *out_staging = NULL;
  staging =
      native_service_suffixed_path(final_path, ".databind-native.tmp");
  if (staging == NULL) return NULL;
  native_service_unlink_if_exists(staging);
  file = fopen(staging, "wb");
  if (file == NULL) {
    free(staging);
    return NULL;
  }
  *out_staging = staging;
  return file;
}

static int native_service_close(FILE **file) {
  int ok = 1;
  if (file == NULL || *file == NULL) return 0;
  if (fflush(*file) != 0) ok = 0;
  if (fclose(*file) != 0) ok = 0;
  *file = NULL;
  return ok;
}

static int native_service_prepare_backup(native_service_output *output) {
  if (output == NULL || !native_service_text_valid(output->final_path) ||
      output->staging_path == NULL)
    return 0;
  output->backup_path =
      native_service_suffixed_path(output->final_path, ".databind-native.bak");
  if (output->backup_path == NULL) return 0;
  native_service_unlink_if_exists(output->backup_path);
  if (cmeta_fs_access(output->final_path, SALTS_FS_ACCESS_EXISTS) == 0) {
    if (cmeta_fs_rename(output->final_path, output->backup_path) != 0)
      return 0;
    output->had_original = 1;
  }
  return 1;
}

static void native_service_rollback(native_service_output *output) {
  if (output == NULL) return;
  if (output->published)
    native_service_unlink_if_exists(output->final_path);
  if (output->had_original && output->backup_path != NULL)
    (void)cmeta_fs_rename(output->backup_path, output->final_path);
  else
    native_service_unlink_if_exists(output->backup_path);
  native_service_unlink_if_exists(output->staging_path);
}

static void native_service_output_clear(native_service_output *output) {
  if (output == NULL) return;
  free(output->staging_path);
  free(output->backup_path);
  memset(output, 0, sizeof(*output));
}

static int native_service_commit(
    native_service_output *outputs, size_t count) {
  size_t i;
  size_t prepared = 0u;
  size_t published = 0u;

  if (outputs == NULL || count == 0u) return 0;
  for (i = 0u; i < count; ++i) {
    if (!native_service_prepare_backup(&outputs[i])) goto fail;
    ++prepared;
  }
  for (i = 0u; i < count; ++i) {
    if (cmeta_fs_rename(
            outputs[i].staging_path, outputs[i].final_path) != 0)
      goto fail;
    outputs[i].published = 1;
    ++published;
  }
  for (i = 0u; i < count; ++i) {
    native_service_unlink_if_exists(outputs[i].backup_path);
    free(outputs[i].staging_path);
    outputs[i].staging_path = NULL;
    free(outputs[i].backup_path);
    outputs[i].backup_path = NULL;
  }
  return 1;

fail:
  (void)prepared;
  (void)published;
  for (i = count; i > 0u; --i)
    native_service_rollback(&outputs[i - 1u]);
  return 0;
}

static int native_service_write_include(FILE *file, const char *name) {
  const unsigned char *p = (const unsigned char *)name;
  if (file == NULL || name == NULL || fputc('"', file) == EOF) return 0;
  for (; *p != '\0'; ++p) {
    if (*p == '\\' || *p == '"') {
      if (fputc('\\', file) == EOF) return 0;
    }
    if (*p < 0x20u || *p == 0x7fu || fputc((int)*p, file) == EOF)
      return 0;
  }
  return fputc('"', file) != EOF;
}

static int native_service_header_guard(
    const IdlContract *contract, char *out, size_t out_size) {
  static const char suffix[] = "_DATABIND_SERVICE_NATIVE_H";
  const char *name = contract != NULL ? contract->name : NULL;
  size_t used = 0u;
  size_t i;
  if (!native_service_text_valid(name) || out == NULL || out_size == 0u)
    return 0;
  for (i = 0u; name[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)name[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_'))
      return 0;
    if (used + 1u >= out_size) return 0;
    out[used++] =
        (char)(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
  }
  if (used > SIZE_MAX - sizeof(suffix) ||
      used + sizeof(suffix) > out_size)
    return 0;
  memcpy(out + used, suffix, sizeof(suffix));
  return 1;
}

/* Contract-only Native Service ABI. Unlike the explicitly selected legacy
 * Binary generator, this publishes exact NativeSourceIR Record values and
 * caller-owned CMeta/DataBind bindings. No Record_t aliases or implicit wire
 * descriptors are emitted. */
static int native_service_contract_record_admitted(
    const databind_native_source_ir *types, const char *name) {
  size_t i, j;
  if (!types || !name) return 0;
  for (i = 0u; i < types->record_count; ++i) {
    const databind_native_source_record *record = &types->records[i];
    if (strcmp(name, record->name) != 0) continue;
    for (j = 0u; j < record->field_count; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      if (field->optional || field->nullable ||
          (field->ownership != DATABIND_NATIVE_TRIVIAL &&
           field->ownership != DATABIND_NATIVE_OWNED_TEXT &&
           field->ownership != DATABIND_NATIVE_OWNED_BYTES))
        return 0;
    }
    return 1;
  }
  return 0;
}

static int native_service_contract_admitted(
    const IdlContract *contract,
    const databind_compiler_service_native_ir *services) {
  databind_native_source_ir types = {0};
  size_t i;
  int ok = 0;
  if (!contract || !services ||
      databind_native_source_ir_build(contract, &types) != 0)
    return 0;
  ok = 1;
  for (i = 0u; i < services->operation_count; ++i) {
    const databind_compiler_service_native_operation *op =
        &services->operations[i];
    if (op->error_count || op->request_presence_count ||
        op->request_null_count || op->response_presence_count ||
        op->response_null_count ||
        !native_service_contract_record_admitted(&types, op->request_type) ||
        !native_service_contract_record_admitted(&types, op->response_type)) {
      fprintf(stderr,
              "Contract-only Native Service requires complete VALUE records "
              "without optional/null/typed-error state: %s\n",
              op->qualified_operation ? op->qualified_operation : "<unknown>");
      ok = 0;
      break;
    }
  }
  databind_native_source_ir_destroy(&types);
  return ok;
}

static int native_service_write_contract_header(
    FILE *file, const IdlContract *contract,
    const databind_compiler_native_service_config *config,
    const databind_compiler_service_native_ir *services) {
  char guard[320];
  size_t i;
  if (!file || !contract || !config || !services ||
      !native_service_header_guard(contract, guard, sizeof(guard)))
    return 0;
  if (fprintf(file, "#ifndef %s\n#define %s\n\n"
                    "#ifndef DATABIND_NATIVE_ENABLE_CMETA\n"
                    "#define DATABIND_NATIVE_ENABLE_CMETA\n#endif\n"
                    "#ifndef DATABIND_NATIVE_ENABLE_DATABIND\n"
                    "#define DATABIND_NATIVE_ENABLE_DATABIND\n#endif\n",
              guard, guard) < 0 ||
      fputs("#include ", file) == EOF ||
      !native_service_write_include(file, config->native_header) ||
      fputs("\n#include <data_bind_binding_plan.h>\n"
            "#include <cmeta/function.h>\n"
            "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", file) == EOF)
    return 0;
  for (i = 0u; i < services->operation_count; ++i) {
    const databind_compiler_service_native_operation *op =
        &services->operations[i];
    if (fprintf(file,
        "int %s(const %s *request, %s *response);\n"
        "typedef struct %s__native_owner {\n"
        "    %s_native_cmeta_binding request_metadata;\n"
        "    %s_native_cmeta_binding response_metadata;\n"
        "    DataBindNativeTypeBinding request_binding;\n"
        "    DataBindNativeTypeBinding response_binding;\n"
        "} %s__native_owner;\n"
        "const cmeta_function_desc *%s__databind_function(void);\n"
        "const cmeta_function_abi_desc *%s__databind_function_abi(void);\n"
        "const DataBindNativeExecution *%s__databind_execution(void);\n"
        "DataBindStatus %s__databind_native_binding(\n"
        "    %s__native_owner *owner,\n"
        "    DataBindServiceNativeBinding *service_out,\n"
        "    DataBindError *error);\n\n",
        op->symbol, op->request_type, op->response_type,
        op->symbol, op->request_type, op->response_type,
        op->symbol, op->symbol, op->symbol, op->symbol,
        op->symbol, op->symbol) < 0)
      return 0;
  }
  return fprintf(file, "#ifdef __cplusplus\n}\n#endif\n\n"
                       "#endif /* %s */\n", guard) >= 0;
}

static int native_service_write_contract_source(
    FILE *file, const databind_compiler_native_service_config *config,
    const databind_compiler_service_native_ir *services) {
  const char *header;
  size_t i;
  if (!file || !config || !services ||
      !(header = native_service_basename(config->header_output)) ||
      fputs("#include ", file) == EOF ||
      !native_service_write_include(file, header) ||
      fputs("\n#include <string.h>\n\n", file) == EOF)
    return 0;
  for (i = 0u; i < services->operation_count; ++i) {
    const databind_compiler_service_native_operation *op =
        &services->operations[i];
    /* Pointer reflection reuses the exact NativeSourceIR storage TypeDesc.
     * Its Schema/version identity comes from the generated Contract header,
     * never from the legacy Record_t Binary ABI. */
    if (fprintf(file,
      "static const cmeta_type_desc %s__request_ptr_type = {\n"
      "    .name = \"const %s *\", .size = sizeof(const %s *),\n"
      "    .align = _Alignof(const %s *), .kind = CMETA_T_POINTER,\n"
      "    .pointee = &%s_native_cmeta_type\n"
      "};\n"
      "static const cmeta_type_desc %s__response_ptr_type = {\n"
      "    .name = \"%s *\", .size = sizeof(%s *),\n"
      "    .align = _Alignof(%s *), .kind = CMETA_T_POINTER,\n"
      "    .pointee = &%s_native_cmeta_type\n"
      "};\n"
      "CMETA_FUNCTION_METADATA_AS_ABI_RESULT(\n"
      "    %s, \"%s\", unknown, &cmeta_type_int, CMETA_ABI_SCALAR,\n"
      "    CMETA_RESULT_VALUE,\n"
      "    (const %s *, request,\n"
      "     CMETA_PARAM_IN | CMETA_PARAM_BORROWED,\n"
      "     &%s__request_ptr_type, CMETA_ABI_OBJECT_POINTER),\n"
      "    (%s *, response,\n"
      "     CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,\n"
      "     &%s__response_ptr_type, CMETA_ABI_OBJECT_POINTER));\n",
      op->symbol, op->request_type, op->request_type, op->request_type,
      op->request_type,
      op->symbol, op->response_type, op->response_type, op->response_type,
      op->response_type,
      op->symbol, op->qualified_operation,
      op->request_type, op->symbol,
      op->response_type, op->symbol) < 0)
      return 0;
    if (fprintf(file,
      "static bool DATA_BIND_NATIVE_CALL %s__databind_invoke(\n"
      "    void *context, void *return_storage, void *const *params,\n"
      "    size_t param_count) {\n"
      "    int status;\n"
      "    (void)context;\n"
      "    if (!return_storage || !params || param_count != 2u ||\n"
      "        !params[0] || !params[1]) return false;\n"
      "    status = %s((const %s *)params[0], (%s *)params[1]);\n"
      "    *(int *)return_storage = status;\n"
      "    return true;\n"
      "}\n"
      "static const DataBindNativeExecution %s__execution_meta = {\n"
      "    sizeof(DataBindNativeExecution), DATA_BIND_NATIVE_EXECUTION_ABI_VERSION,\n"
      "    &%s__function_meta, &%s__function_abi_meta, NULL,\n"
      "    %s__databind_invoke\n"
      "};\n"
      "const cmeta_function_desc *%s__databind_function(void) {\n"
      "    return &%s__function_meta;\n"
      "}\n"
      "const cmeta_function_abi_desc *%s__databind_function_abi(void) {\n"
      "    return &%s__function_abi_meta;\n"
      "}\n"
      "const DataBindNativeExecution *%s__databind_execution(void) {\n"
      "    return &%s__execution_meta;\n"
      "}\n",
      op->symbol, op->symbol, op->request_type, op->response_type,
      op->symbol, op->symbol, op->symbol, op->symbol,
      op->symbol, op->symbol, op->symbol, op->symbol,
      op->symbol, op->symbol) < 0)
      return 0;
    if (fprintf(file,
      "DataBindStatus %s__databind_native_binding(\n"
      "    %s__native_owner *owner,\n"
      "    DataBindServiceNativeBinding *service_out,\n"
      "    DataBindError *error) {\n"
      "    (void)error;\n"
      "    if (!owner || !service_out) return DATA_BIND_ERR_INVALID_ARG;\n"
      "    if (%s_native_type_binding(&owner->request_metadata,\n"
      "            &owner->request_binding) != 0 ||\n"
      "        %s_native_type_binding(&owner->response_metadata,\n"
      "            &owner->response_binding) != 0)\n"
      "        return DATA_BIND_ERR_SCHEMA;\n"
      "    *service_out = (DataBindServiceNativeBinding){\n"
      "        sizeof(DataBindServiceNativeBinding),\n"
      "        DATA_BIND_BINDING_PLAN_ABI_VERSION,\n"
      "        &%s__function_meta,\n"
      "        &owner->request_binding, &owner->response_binding,\n"
      "        NULL, 0u, SIZE_MAX, 0u, 0u, 0u\n"
      "    };\n"
      "    return DATA_BIND_OK;\n"
      "}\n\n",
      op->symbol, op->symbol, op->request_type, op->response_type,
      op->symbol) < 0)
      return 0;
  }
  return 1;
}

static int native_service_write_header(
    FILE *file,
    const IdlContract *contract,
    const databind_compiler_native_service_config *config,
    const databind_compiler_service_native_ir *ir) {
  char guard[320];
  size_t i;

  if (file == NULL || contract == NULL || config == NULL || ir == NULL ||
      !native_service_text_valid(config->native_header) ||
      !native_service_header_guard(contract, guard, sizeof(guard)))
    return 0;

  if (fprintf(file, "#ifndef %s\n#define %s\n\n", guard, guard) < 0 ||
      fputs("#include ", file) == EOF ||
      !native_service_write_include(file, config->native_header) ||
      fputs(
          "\n#include <data_bind_binding_plan.h>\n"
          "#include <cmeta/function.h>\n"
          "#include <cflow/function_projection.h>\n\n"
          "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];
    if (databind_compiler_service_native_emit_prototype(
            file, operation) != 0 ||
        fprintf(
            file,
            "const cmeta_function_desc *%s__databind_function(void);\n"
            "const cmeta_function_abi_desc *%s__databind_function_abi(void);\n"
            "const DataBindNativeExecution *%s__databind_execution(void);\n"
            "cflow_function_projection_status %s__databind_cflow_projection(\n"
            "    cflow_function_typed_adapter_projection *out);\n"
            "DataBindStatus %s__databind_native_binding(\n"
            "    DataBindNativeTypeBinding *request_out,\n"
            "    DataBindNativeTypeBinding *response_out,\n"
            "    DataBindServiceNativeBinding *service_out,\n"
            "    DataBindError *error);\n\n",
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return 0;
  }

  return fprintf(
             file,
             "#ifdef __cplusplus\n}\n#endif\n\n"
             "#endif /* %s */\n",
             guard) >= 0;
}

static int native_service_write_source(
    FILE *file,
    const databind_compiler_native_service_config *config,
    const databind_compiler_service_native_ir *ir) {
  const char *header;
  size_t i;
  if (file == NULL || config == NULL || ir == NULL)
    return 0;
  header = native_service_basename(config->header_output);
  if (!native_service_text_valid(header) ||
      fputs("#include ", file) == EOF ||
      !native_service_write_include(file, header) ||
      fputs("\n#include <string.h>\n\n", file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, &ir->operations[i], 1) != 0 ||
        fputc('\n', file) == EOF ||
        databind_compiler_service_native_emit_execution(
            file, &ir->operations[i], 1) != 0 ||
        databind_compiler_service_native_emit_binding(
            file, &ir->operations[i]) != 0 ||
        databind_compiler_service_native_emit_cflow_projection(
            file, &ir->operations[i]) != 0 ||
        fputc('\n', file) == EOF)
      return 0;
  }
  return 1;
}

/* The compiler coordinator reserves unique zero-byte stage siblings before
 * rendering. Accept its empty reservation, never clobber populated stages. */
static int native_service_stage_ready(const char *path) {
  FILE *file;
  long size;
  if (!native_service_text_valid(path)) return 0;
  file = fopen(path, "rb");
  if (file == NULL) return cmeta_fs_access(path, SALTS_FS_ACCESS_EXISTS) != 0;
  if (fseek(file, 0, SEEK_END) != 0) {
    (void)fclose(file);
    return 0;
  }
  size = ftell(file);
  if (fclose(file) != 0) return 0;
  return size == 0;
}

/* Coordinator-only render step: stage destinations are provided by the caller.
 * Final names remain in request/config for generated source includes.
 * Ownership of staged files and their publication stays with the coordinator.
 */
int databind_compiler_native_service_render_staged(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    const char *header_stage,
    const char *source_stage) {
  const IdlContract *contract = input != NULL ? input->contract : NULL;
  const databind_compiler_native_service_config *config =
      request != NULL ? (const databind_compiler_native_service_config *)request->config : NULL;
  databind_compiler_service_native_ir ir = {0};
  FILE *header_file = NULL;
  FILE *source_file = NULL;
  int result = -1;

  if (contract == NULL || request == NULL ||
      request->id.axis != DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT ||
      request->id.kind != DATABIND_COMPILER_ARTIFACT_NATIVE ||
      config == NULL || !native_service_text_valid(config->native_header) ||
      !native_service_text_valid(config->header_output) ||
      !native_service_text_valid(request->output) ||
      !native_service_text_valid(header_stage) ||
      !native_service_text_valid(source_stage) ||
      strcmp(header_stage, source_stage) == 0 ||
      strcmp(header_stage, config->header_output) == 0 ||
      strcmp(source_stage, request->output) == 0 ||
      strcmp(header_stage, request->output) == 0 ||
      strcmp(source_stage, config->header_output) == 0 ||
      strcmp(config->header_output, request->output) == 0)
    return -1;

  if (databind_compiler_service_native_build(contract, &ir) != 0 ||
      ir.operations == NULL || ir.operation_count == 0u)
    goto cleanup;
  if (input->binary_format == NULL &&
      !native_service_contract_admitted(contract, &ir))
    goto cleanup;

  /* Empty, unique coordinator reservations are valid; populated files are not. */
  if (!native_service_stage_ready(header_stage) ||
      !native_service_stage_ready(source_stage))
    goto cleanup;
  header_file = fopen(header_stage, "wb");
  if (header_file == NULL) goto cleanup;
  source_file = fopen(source_stage, "wb");
  if (source_file == NULL) goto cleanup;
  if (input->binary_format == NULL
          ? (!native_service_write_contract_header(
                 header_file, contract, config, &ir) ||
             !native_service_write_contract_source(source_file, config, &ir))
          : (!native_service_write_header(header_file, contract, config, &ir) ||
             !native_service_write_source(source_file, config, &ir)))
    goto cleanup;
  if (!native_service_close(&header_file) ||
      !native_service_close(&source_file))
    goto cleanup;
  result = 0;
cleanup:
  if (header_file != NULL) (void)fclose(header_file);
  if (source_file != NULL) (void)fclose(source_file);
  databind_compiler_service_native_destroy(&ir);
  return result;
}

int databind_compiler_native_service_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  const IdlContract *contract = input != NULL ? input->contract : NULL;
  const databind_compiler_native_service_config *config =
      request != NULL
          ? (const databind_compiler_native_service_config *)request->config
          : NULL;
  databind_compiler_service_native_ir ir = {0};
  native_service_output outputs[2] = {{0}};
  FILE *header_file = NULL;
  FILE *source_file = NULL;
  char *header_staging = NULL;
  char *source_staging = NULL;
  int result = -1;
  (void)context;

  if (contract == NULL || request == NULL ||
      request->id.axis != DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT ||
      request->id.kind != DATABIND_COMPILER_ARTIFACT_NATIVE ||
      config == NULL ||
      !native_service_text_valid(request->output) ||
      !native_service_text_valid(config->native_header) ||
      !native_service_text_valid(config->header_output) ||
      strcmp(request->output, config->header_output) == 0)
    return -1;

  if (databind_compiler_service_native_build(contract, &ir) != 0 ||
      ir.operations == NULL || ir.operation_count == 0u)
    goto cleanup;

  header_file = native_service_open_staging(
      config->header_output, &header_staging);
  if (header_file == NULL) goto cleanup;
  source_file = native_service_open_staging(
      request->output, &source_staging);
  if (source_file == NULL) goto cleanup;

  if (!native_service_write_header(
          header_file, contract, config, &ir) ||
      !native_service_write_source(source_file, config, &ir))
    goto cleanup;

  if (!native_service_close(&header_file) ||
      !native_service_close(&source_file))
    goto cleanup;

  outputs[0].final_path = config->header_output;
  outputs[0].staging_path = header_staging;
  outputs[1].final_path = request->output;
  outputs[1].staging_path = source_staging;
  header_staging = NULL;
  source_staging = NULL;

  if (!native_service_commit(
          outputs, sizeof(outputs) / sizeof(outputs[0])))
    goto cleanup;

  result = 0;

cleanup:
  if (header_file != NULL) fclose(header_file);
  if (source_file != NULL) fclose(source_file);
  native_service_unlink_if_exists(header_staging);
  native_service_unlink_if_exists(source_staging);
  free(header_staging);
  free(source_staging);
  native_service_output_clear(&outputs[0]);
  native_service_output_clear(&outputs[1]);
  databind_compiler_service_native_destroy(&ir);
  return result;
}

const databind_compiler_projection_backend
    DATABIND_COMPILER_NATIVE_SERVICE_BACKEND = {
        {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
         DATABIND_COMPILER_ARTIFACT_NATIVE},
        "native",
        databind_compiler_native_service_generate,
        NULL,
        DATABIND_COMPILER_OUTPUT_STAGED_MULTI,
};

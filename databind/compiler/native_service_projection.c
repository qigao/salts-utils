#include "native_service_projection.h"

#include "service_native.h"
#include "native_source_ir.h"
#include "cmeta_fs.h"
#include <fmt.h>

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

int databind_compiler_native_contract_admitted(
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

int databind_compiler_native_contract_emit_header_op(
    FILE *file, const databind_compiler_service_native_operation *op) {
  if (!file || !op || !op->symbol || !op->request_type || !op->response_type) return 0;
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
  return 1;
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
  for (i = 0u; i < services->operation_count; ++i)
    if (!databind_compiler_native_contract_emit_header_op(file, &services->operations[i])) return 0;
  return fprintf(file, "#ifdef __cplusplus\n}\n#endif\n\n"
                       "#endif /* %s */\n", guard) >= 0;
}

int databind_compiler_native_contract_emit_reflection(
    FILE *file, const databind_compiler_service_native_operation *op) {
  if (!file || !op || !op->symbol || !op->request_type || !op->response_type) return 0;
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

  return 1;
}

int databind_compiler_native_contract_emit_invoke(
    FILE *file, const databind_compiler_service_native_operation *op) {
  if (!file || !op || !op->symbol || !op->request_type || !op->response_type) return 0;
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

  return 1;
}

int databind_compiler_native_contract_emit_binding(
    FILE *file, const databind_compiler_service_native_operation *op) {
  if (!file || !op || !op->symbol || !op->request_type || !op->response_type) return 0;
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
  return 1;
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
    const databind_compiler_service_native_operation *op = &services->operations[i];
    if (!databind_compiler_native_contract_emit_reflection(file, op) ||
        !databind_compiler_native_contract_emit_invoke(file, op) ||
        !databind_compiler_native_contract_emit_binding(file, op))
      return 0;
  }
  return 1;
}

static size_t injection_count(const IdlService *service) {
  return idl_annotation_count(service->annotations, service->annotation_count, "inject");
}

static int injection_identifier(const char *s) {
  if (s == NULL || !((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_')) return 0;
  for (++s; *s != '\0'; ++s)
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
          (*s >= '0' && *s <= '9') || *s == '_')) return 0;
  return 1;
}

int databind_compiler_native_injection_valid(const IdlService *service) {
  if (service == NULL || injection_count(service) > 16u) return 0;
  for (size_t i = 0u; i < service->annotation_count; ++i) {
    const IdlAnnotation *a = &service->annotations[i];
    if (strcmp(a->name, "inject") != 0 && strcmp(a->name, "http") != 0 &&
        strncmp(a->name, "app_", 4u) != 0) continue;
    if (strcmp(a->name, "inject") != 0 || a->bare || a->argument_count != 3u ||
        !injection_identifier(a->arguments[0]) || !injection_identifier(a->arguments[1]) ||
        a->arguments[2] == NULL || a->arguments[2][0] == '\0') return 0;
    for (const char *p = a->arguments[2]; *p != '\0'; ++p)
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.' || *p == '/')) return 0;
    for (size_t j = 0u; j < i; ++j) {
      const IdlAnnotation *b = &service->annotations[j];
      if (strcmp(b->name, "inject") == 0 &&
          (strcmp(a->arguments[0], b->arguments[0]) == 0 ||
           strcmp(a->arguments[1], b->arguments[1]) == 0)) return 0;
    }
  }
  if (injection_count(service) != 0u)
    for (size_t i = 0u; i < service->operation_count; ++i)
      if (service->operations[i].error_count != 0u) return 0;
  return 1;
}

static tstr injection_type(const IdlContract *contract, const IdlService *service) {
  return tstr_format("databind_{}_{}_{}_{}_dependencies",
      strlen(contract->name), contract->name, strlen(service->name), service->name);
}

static int injection_header(FILE *file, const IdlService *service, const char *type) {
  size_t count = injection_count(service);
  for (size_t i = 0u; i < count; ++i) {
    const IdlAnnotation *a = idl_annotation_find(service->annotations, service->annotation_count, "inject", i);
    if (fputs("#include ", file) == EOF || !native_service_write_include(file, a->arguments[2]) ||
        fputc('\n', file) == EOF) return 0;
  }
  if (fprintf(file, "\n/* Borrowed interfaces, valid for this application's lifetime. */\n"
      "typedef struct %s {\n", type) < 0) return 0;
  for (size_t i = 0u; i < count; ++i) {
    const IdlAnnotation *a = idl_annotation_find(service->annotations, service->annotation_count, "inject", i);
    if (fprintf(file, "  %s %s;\n", a->arguments[1], a->arguments[0]) < 0) return 0;
  }
  return fprintf(file, "} %s;\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
      "const salts_component_provider_binding *%s_component(void);\n"
      "#ifdef __cplusplus\n}\n#endif\n\n", type, type) >= 0;
}

static int injection_source(FILE *file, const IdlService *service, const char *type) {
  size_t count = injection_count(service);
  /* The dependency record is opaque to data reflection: no field access or
   * value lifecycle is granted. The component's ObjectRef alone owns it. */
  if (fprintf(file,
      "static const cmeta_type_identity %s_id = CMETA_TYPE_ID_ATOM_INIT(\"%s\");\n"
      "static const cmeta_type_desc %s_type = {\"%s\", sizeof(%s), _Alignof(%s), CMETA_T_OBJECT, NULL, NULL, &%s_id};\n"
      "static const cmeta_type_desc %s_pointer = {\"const %s *\", sizeof(const %s *), _Alignof(const %s *), CMETA_T_POINTER, &%s_type, NULL, NULL};\n"
      "static const cmeta_struct_desc %s_layout = {\"%s\", sizeof(%s), _Alignof(%s), NULL, 0u};\n"
      "static const cmeta_data_struct_shape %s_shape = {&%s_layout, NULL, 0u};\n"
      "static const cmeta_data_desc %s_data = {\n"
      "  .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,\n"
      "  .stable_id = \"%s\", .display_name = \"%s\", .kind = CMETA_DATA_STRUCT,\n"
      "  .storage_type = &%s_type, .shape = &%s_shape};\n",
      type,type, type,type,type,type,type, type,type,type,type,type,
      type,type,type,type, type,type, type,type,type,type,type) < 0) return 0;
  if (fprintf(file, "cmeta_component(%s", type) < 0) return 0;
  for (size_t i = 0u; i < count; ++i) {
    const IdlAnnotation *a = idl_annotation_find(service->annotations, service->annotation_count, "inject", i);
    if (fprintf(file, "%s cmeta_requires(%s)", i == 0u ? "," : "", a->arguments[1]) < 0) return 0;
  }
  if (fprintf(file, ");\nstatic void %s_destroy(void *context, void *object) { (void)context; free(object); }\n"
      "static const cmeta_object_lifecycle %s_lifecycle = {sizeof(cmeta_object_lifecycle), NULL, NULL, NULL, %s_destroy};\n"
      "static cmeta_status SALTS_COMPONENT_CALL %s_create(void *context, const cmeta_data_desc *config_data,\n"
      "    const void *config, const salts_component_dependency *dependencies, size_t count, cmeta_object_ref *out) {\n"
      "  (void)context;\n  if (config_data != NULL || config != NULL || count != %zuu) return CMETA_INVALID_ARGUMENT;\n"
      "  %s *instance = calloc(1u, sizeof(*instance));\n"
      "  if (instance == NULL) return CMETA_OUT_OF_MEMORY;\n"
      "  cmeta_status status = CMETA_OK;\n"
      "  const salts_component_dependency *dependency = NULL;\n"
      "  cmeta_interface_projection projection = CMETA_INTERFACE_PROJECTION_INIT;\n",
      type,type,type,type,count,type) < 0) return 0;
  for (size_t i = 0u; i < count; ++i) {
    const IdlAnnotation *a = idl_annotation_find(service->annotations, service->annotation_count, "inject", i);
    const char *member = a->arguments[0], *interface_name = a->arguments[1];
    if (fprintf(file,
        "  if (salts_component_dependency_find(dependencies, count, %s_interface(), &dependency) != SALTS_COMPONENT_OK) {\n"
        "    status = CMETA_TRAIT_MISSING; goto fail;\n  }\n"
        "  status = cmeta_object_interface_project_borrowed(dependency->provider_instance,\n"
        "      dependency->provider_interfaces, %s_interface(), &projection);\n"
        "  if (status != CMETA_OK) goto fail;\n"
        "  instance->%s = %s_bind(projection.self, (const %s_vtable *)projection.dispatch);\n"
        "  if (!%s_valid(&instance->%s)) { status = CMETA_TYPE_MISMATCH; goto fail; }\n",
        interface_name,interface_name,member,interface_name,interface_name,interface_name,member) < 0) return 0;
  }
  return fprintf(file,
      "  status = cmeta_object_borrow(out, instance, &%s_data, NULL);\n"
      "  if (status != CMETA_OK) goto fail;\n"
      "  status = cmeta_object_take(out, &%s_lifecycle);\n"
      "  if (status == CMETA_OK) return status;\n"
      "  cmeta_object_release(out);\n"
      "fail:\n  free(instance);\n  return status;\n}\n"
      "const salts_component_provider_binding *%s_component(void) {\n"
      "  static const salts_component_provider_binding provider = {sizeof(salts_component_provider_binding),\n"
      "      SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION, cmeta_component_meta(%s),\n"
      "      NULL, NULL, %s_create, NULL, NULL};\n  return &provider;\n}\n\n",
      type,type,type,type,type) >= 0;
}

static int injection_execution(FILE *file, const databind_compiler_service_native_operation *op, const char *type) {
  const char *s = op->symbol;
  if (fprintf(file,
      "typedef int (*%s_receiver_fn)(const %s *, const %s_t *, %s_t *);\n"
      "CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&%s, %s_receiver_fn), \"injected native signature mismatch\");\n"
      "CMETA_FUNCTION_METADATA_AS_ABI_RESULT(%s_receiver, \"%s.receiver\", unknown,\n"
      "    &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,\n"
      "    (const %s *, self, CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_RECEIVER, &%s_pointer, CMETA_ABI_OBJECT_POINTER),\n"
      "    (const %s_t *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED, &%s__request_ptr_type, CMETA_ABI_OBJECT_POINTER),\n"
      "    (%s_t *, response, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, &%s__response_ptr_type, CMETA_ABI_OBJECT_POINTER));\n"
      "static bool DATA_BIND_NATIVE_CALL %s_injected_invoke(void *context, void *result, void *const *params, size_t count) {\n"
      "  if (context == NULL || result == NULL || params == NULL || count != 2u || params[0] == NULL || params[1] == NULL) return false;\n"
      "  *(int *)result = %s((const %s *)context, (const %s_t *)params[0], (%s_t *)params[1]);\n"
      "  return true;\n}\n",
      s,type,op->request_type,op->response_type,s,s,s,op->qualified_operation,type,type,
      op->request_type,s,op->response_type,s,s,s,type,op->request_type,op->response_type) < 0) return 0;
  return fprintf(file,
      "cmeta_status %s__databind_bind_execution(const cmeta_object_ref *instance, DataBindNativeExecution *out) {\n"
      "  static const bool bound[] = {true, false, false};\n"
      "  if (out == NULL) return CMETA_INVALID_ARGUMENT;\n"
      "  *out = (DataBindNativeExecution)DATA_BIND_NATIVE_EXECUTION_INIT;\n"
      "  if (!cmeta_object_ref_valid(instance) || !cmeta_data_desc_equal(instance->data, &%s_data)) return CMETA_TYPE_MISMATCH;\n"
      "  if (!cmeta_function_receiver_projection_valid(&%s_receiver__function_meta, &%s__function_meta) ||\n"
      "      !cmeta_function_projection_valid(&%s_receiver__function_abi_meta, &%s__function_abi_meta, bound, 3u)) return CMETA_TYPE_MISMATCH;\n"
      "  *out = (DataBindNativeExecution){sizeof(*out), DATA_BIND_NATIVE_EXECUTION_ABI_VERSION,\n"
      "      &%s__function_meta, &%s__function_abi_meta, instance->object, %s_injected_invoke};\n"
      "  return CMETA_OK;\n}\n\n", s,type,s,s,s,s,s,s,s) >= 0;
}

static int native_service_catalog_prefix(FILE *file, const char *header) {
  const char *name = native_service_basename(header);
  const char *end = strrchr(name, '.');
  if (end == NULL) end = name + strlen(name);
  if (fputs("databind_", file) == EOF) return 0;
  for (const char *p = name; p < end; ++p) {
    int ch = ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9')) ? *p : '_';
    if (fputc(ch, file) == EOF) return 0;
  }
  return 1;
}

static int native_service_write_catalog(FILE *file, const IdlContract *contract,
    const databind_compiler_native_service_config *config,
    const databind_compiler_service_native_ir *ir) {
  if (fputs("/* Producer-owned operation enumeration; X receives service, operation,\n"
      " * exact symbol, request type and response type. */\n#define ", file) == EOF ||
      !native_service_catalog_prefix(file, config->native_header) ||
      fputs("_SERVICES(X)", file) == EOF) return 0;
  for (size_t i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (fprintf(file, " \\\n  X(\"%s\", \"%s\", %s, %s, %s)",
        op->service_name, op->operation_name, op->symbol, op->request_type, op->response_type) < 0) return 0;
  }
  if (fputs("\n#define ", file) == EOF ||
      !native_service_catalog_prefix(file, config->native_header) ||
      fputs("_APPLICATION_SERVICES(X)", file) == EOF) return 0;
  for (size_t i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    const IdlService *service = idl_contract_find_service(contract, op->service_name);
    tstr type = injection_count(service) != 0u ? injection_type(contract, service) : NULL;
    if (injection_count(service) != 0u && type == NULL) return 0;
    int ok = fprintf(file, " \\\n  X(\"%s\", \"%s\", %s, %s, %s, %s%s)", op->service_name,
        op->operation_name, op->symbol, op->request_type, op->response_type,
        type != NULL ? type : "NULL", type != NULL ? "_component" : "") >= 0;
    tstr_free(type);
    if (!ok) return 0;
  }
  return fputs("\n#define ", file) != EOF &&
      native_service_catalog_prefix(file, config->native_header) &&
      fprintf(file, "_CODEC %s_codec_create\n\n", contract->name) >= 0;
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
          "#include <salts/component_abi.h>\n"
          "#include <cflow/function_projection.h>\n\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    if (injection_count(service) == 0u) continue;
    tstr type = injection_type(contract, service);
    if (type == NULL) return 0;
    int ok = injection_header(file, service, type);
    tstr_free(type);
    if (!ok) return 0;
  }
  if (fputs("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", file) == EOF) return 0;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];
    const IdlService *service = idl_contract_find_service(contract, operation->service_name);
    if (injection_count(service) != 0u) {
      tstr type = injection_type(contract, service);
      if (type == NULL) return 0;
      int ok = fprintf(file, "int %s(const %s *dependencies, const %s_t *request, %s_t *response);\n",
          operation->symbol, type, operation->request_type, operation->response_type) >= 0;
      tstr_free(type);
      if (!ok) return 0;
    } else if (databind_compiler_service_native_emit_prototype(file, operation) != 0 ||
        fprintf(file, "const DataBindNativeExecution *%s__databind_execution(void);\n"
            "cflow_function_projection_status %s__databind_cflow_projection(cflow_function_typed_adapter_projection *out);\n",
            operation->symbol, operation->symbol) < 0) return 0;
    if (
        fprintf(
            file,
            "const cmeta_function_desc *%s__databind_function(void);\n"
            "const cmeta_function_abi_desc *%s__databind_function_abi(void);\n"
            "cmeta_status %s__databind_bind_execution(const cmeta_object_ref *instance, DataBindNativeExecution *out);\n"
            "DataBindStatus %s__databind_native_binding(\n"
            "    DataBindNativeTypeBinding *request_out,\n"
            "    DataBindNativeTypeBinding *response_out,\n"
            "    DataBindServiceNativeBinding *service_out,\n"
            "    DataBindError *error);\n\n",
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return 0;
  }

  if (!native_service_write_catalog(file, contract, config, ir)) return 0;

  return fprintf(
             file,
             "#ifdef __cplusplus\n}\n#endif\n\n"
             "#endif /* %s */\n",
             guard) >= 0;
}

static int native_service_write_source(
    FILE *file,
    const IdlContract *contract,
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
      fputs("\n#include <string.h>\n#include <stdlib.h>\n\n", file) == EOF)
    return 0;

  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    if (injection_count(service) == 0u) continue;
    tstr type = injection_type(contract, service);
    if (type == NULL) return 0;
    int ok = injection_source(file, service, type);
    tstr_free(type);
    if (!ok) return 0;
  }
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    const IdlService *service = idl_contract_find_service(contract, op->service_name);
    int injected = injection_count(service) != 0u;
    if (databind_compiler_service_native_emit_reflection(
            file, op, 1) != 0 ||
        databind_compiler_service_native_emit_binding(file, op) != 0)
      return 0;
    if (injected) {
      tstr type = injection_type(contract, service);
      if (type == NULL) return 0;
      int ok = injection_execution(file, op, type);
      tstr_free(type);
      if (!ok) return 0;
    } else {
      if (databind_compiler_service_native_emit_execution(file, op, 1) != 0 ||
          databind_compiler_service_native_emit_cflow_projection(file, op) != 0 ||
          fprintf(file, "cmeta_status %s__databind_bind_execution(const cmeta_object_ref *instance, DataBindNativeExecution *out) {\n"
              "  if (out == NULL) return CMETA_INVALID_ARGUMENT;\n"
              "  *out = (DataBindNativeExecution)DATA_BIND_NATIVE_EXECUTION_INIT;\n"
              "  if (instance != NULL) return CMETA_INVALID_ARGUMENT;\n"
              "  *out = *%s__databind_execution();\n  return CMETA_OK;\n}\n",
              op->symbol, op->symbol) < 0) return 0;
    }
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
  for (size_t i = 0u; i < contract->service_count; ++i) {
    if (!databind_compiler_native_injection_valid(&contract->services[i]) ||
        (injection_count(&contract->services[i]) != 0u &&
         input->binary_format == NULL && !config->binary_presentation)) goto cleanup;
  }
  if (input->binary_format == NULL && !config->binary_presentation &&
      !databind_compiler_native_contract_admitted(contract, &ir))
    goto cleanup;

  /* Empty, unique coordinator reservations are valid; populated files are not. */
  if (!native_service_stage_ready(header_stage) ||
      !native_service_stage_ready(source_stage))
    goto cleanup;
  header_file = fopen(header_stage, "wb");
  if (header_file == NULL) goto cleanup;
  source_file = fopen(source_stage, "wb");
  if (source_file == NULL) goto cleanup;
  if (input->binary_format == NULL && !config->binary_presentation
          ? (!native_service_write_contract_header(
                 header_file, contract, config, &ir) ||
             !native_service_write_contract_source(source_file, config, &ir))
          : (!native_service_write_header(header_file, contract, config, &ir) ||
             !native_service_write_source(source_file, contract, config, &ir)))
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

  for (size_t i = 0u; i < contract->service_count; ++i)
    if (!databind_compiler_native_injection_valid(&contract->services[i]) ||
        (injection_count(&contract->services[i]) != 0u &&
         input->binary_format == NULL && !config->binary_presentation)) goto cleanup;

  header_file = native_service_open_staging(
      config->header_output, &header_staging);
  if (header_file == NULL) goto cleanup;
  source_file = native_service_open_staging(
      request->output, &source_staging);
  if (source_file == NULL) goto cleanup;

  if (!native_service_write_header(
          header_file, contract, config, &ir) ||
      !native_service_write_source(source_file, contract, config, &ir))
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

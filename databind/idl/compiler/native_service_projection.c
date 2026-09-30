#include "native_service_projection.h"

#include "service_native.h"
#include "salts_fs.h"

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
      salts_fs_access(path, SALTS_FS_ACCESS_EXISTS) == 0)
    (void)salts_fs_unlink(path);
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
  if (salts_fs_access(output->final_path, SALTS_FS_ACCESS_EXISTS) == 0) {
    if (salts_fs_rename(output->final_path, output->backup_path) != 0)
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
    (void)salts_fs_rename(output->backup_path, output->final_path);
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
    if (salts_fs_rename(
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
          "#include <cmeta/function.h>\n\n"
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
            "DataBindStatus %s__databind_native_binding(\n"
            "    DataBindNativeTypeBinding *request_out,\n"
            "    DataBindNativeTypeBinding *response_out,\n"
            "    DataBindServiceNativeBinding *service_out,\n"
            "    DataBindError *error);\n\n",
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
      fputs("\n\n", file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, &ir->operations[i], 1) != 0 ||
        fputc('\n', file) == EOF ||
        databind_compiler_service_native_emit_binding(
            file, &ir->operations[i]) != 0 ||
        fputc('\n', file) == EOF)
      return 0;
  }
  return 1;
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

  if (databind_compiler_service_native_build(contract, &ir) != 0)
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
};

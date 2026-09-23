#include "plugin_projection.h"

#include "salts_fs.h"
#include "salts_uuid.h"

#include <salts/plugin.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define plugin_fdopen _fdopen
#define plugin_open _open
#define PLUGIN_TEMP_OPEN_FLAGS (_O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY)
#define PLUGIN_TEMP_OPEN_MODE (_S_IREAD | _S_IWRITE)
#else
#include <sys/stat.h>
#include <unistd.h>
#define plugin_fdopen fdopen
#define plugin_open open
#define PLUGIN_TEMP_OPEN_FLAGS (O_WRONLY | O_CREAT | O_EXCL)
#define PLUGIN_TEMP_OPEN_MODE 0666
#endif

#define PLUGIN_TEMP_OUTPUT_ATTEMPTS 16u
#define PLUGIN_NATIVE_SYMBOL_MAX \
  (SALTS_PLUGIN_ID_MAX + SALTS_PLUGIN_EXPORT_ID_MAX + 96u)

static int plugin_create_temporary_output(
    const char *output_path, char **out_temporary_path, FILE **out_file) {
  static const char prefix[] = ".plugin.";
  static const char suffix[] = ".tmp";
  size_t output_length;
  size_t length;
  unsigned attempt;

  if (output_path == NULL || out_temporary_path == NULL || out_file == NULL)
    return 0;

  output_length = strlen(output_path);
  if (output_length > SIZE_MAX - (sizeof(prefix) - 1u) -
                          SALTS_UUID_STRING_LENGTH - sizeof(suffix))
    return 0;
  length = output_length + (sizeof(prefix) - 1u) +
           SALTS_UUID_STRING_LENGTH + sizeof(suffix);

  *out_temporary_path = NULL;
  *out_file = NULL;

  for (attempt = 0u; attempt < PLUGIN_TEMP_OUTPUT_ATTEMPTS; ++attempt) {
    salts_uuid_t uuid;
    char uuid_text[SALTS_UUID_STRING_SIZE];
    char *temporary_path;
    int descriptor;
    FILE *file;

    if (salts_uuid_v4_generate(&uuid) != SALTS_OK ||
        salts_uuid_format(&uuid, uuid_text, sizeof(uuid_text)) != SALTS_OK)
      return 0;

    temporary_path = (char *)malloc(length);
    if (temporary_path == NULL) return 0;

    snprintf(temporary_path, length, "%s%s%s%s",
             output_path, prefix, uuid_text, suffix);

    descriptor = plugin_open(
        temporary_path, PLUGIN_TEMP_OPEN_FLAGS, PLUGIN_TEMP_OPEN_MODE);
    if (descriptor < 0) {
      free(temporary_path);
      if (errno == EEXIST) continue;
      return 0;
    }

    file = plugin_fdopen(descriptor, "wb");
    if (file == NULL) {
#ifdef _WIN32
      _close(descriptor);
#else
      close(descriptor);
#endif
      (void)salts_fs_unlink(temporary_path);
      free(temporary_path);
      return 0;
    }

    *out_temporary_path = temporary_path;
    *out_file = file;
    return 1;
  }

  return 0;
}

static const Node *plugin_child(const Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || name == NULL) return NULL;
  if (parent->type == NODE_MAP) {
    for (i = 0u; i < parent->data.map.count; ++i) {
      const Node *child = parent->data.map.items[i];
      if (child != NULL && child->name != NULL &&
          strcmp(child->name, name) == 0)
        return child;
    }
  } else if (parent->type == NODE_LIST) {
    for (i = 0u; i < parent->data.list.count; ++i) {
      const Node *child = parent->data.list.items[i];
      if (child != NULL && child->name != NULL &&
          strcmp(child->name, name) == 0)
        return child;
    }
  }
  return NULL;
}

static const char *plugin_string(const Node *parent, const char *name) {
  const Node *child = plugin_child(parent, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

static int plugin_header_path_valid(const char *value) {
  size_t i;
  if (value == NULL || value[0] == '\0') return 0;
  for (i = 0u; value[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)value[i];
    if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/'))
      return 0;
    if (i >= SALTS_PLUGIN_PATH_MAX) return 0;
  }
  return 1;
}

static int plugin_identifier_valid(const char *value) {
  size_t i;
  if (value == NULL || value[0] == '\0' ||
      !(isalpha((unsigned char)value[0]) || value[0] == '_'))
    return 0;
  for (i = 1u; value[i] != '\0'; ++i)
    if (!(isalnum((unsigned char)value[i]) || value[i] == '_'))
      return 0;
  return 1;
}

static int plugin_positive_u32(const char *text, uint32_t *out) {
  char *end = NULL;
  unsigned long value;
  if (text == NULL || text[0] == '\0' || out == NULL || text[0] == '-')
    return 0;
  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' ||
      value == 0ul || value > UINT32_MAX)
    return 0;
  *out = (uint32_t)value;
  return 1;
}

static int plugin_write_c_string(FILE *out, const char *value) {
  const unsigned char *cursor;
  if (out == NULL || value == NULL) return 0;
  if (fputc('"', out) == EOF) return 0;
  for (cursor = (const unsigned char *)value; *cursor != 0u; ++cursor) {
    unsigned char ch = *cursor;
    if (ch == '"' || ch == '\\') {
      if (fputc('\\', out) == EOF || fputc((int)ch, out) == EOF) return 0;
    } else if (ch == '\n') {
      if (fputs("\\n", out) == EOF) return 0;
    } else if (ch == '\r') {
      if (fputs("\\r", out) == EOF) return 0;
    } else if (ch == '\t') {
      if (fputs("\\t", out) == EOF) return 0;
    } else if (ch < 0x20u || ch == 0x7fu) {
      if (fprintf(out, "\\x%02x", (unsigned)ch) < 0) return 0;
    } else if (fputc((int)ch, out) == EOF) {
      return 0;
    }
  }
  return fputc('"', out) != EOF;
}

static int plugin_write_type_desc(
    FILE *out, const char *symbol, const char *schema_name,
    const char *type_name, int is_const) {
  if (fprintf(out,
      "static const cmeta_type_identity %s_value_id =\n"
      "    CMETA_TYPE_ID_ATOM_INIT(\"tbe.native.%s.%s_t\");\n"
      "static const cmeta_type_desc %s_value_type = {\n"
      "    \"%s_t\", sizeof(%s_t), _Alignof(%s_t),\n"
      "    CMETA_T_OBJECT, NULL, NULL, &%s_value_id\n"
      "};\n"
      "static const cmeta_type_desc %s_ptr_type = {\n",
      symbol, schema_name, type_name,
      symbol, type_name, type_name, type_name, symbol, symbol) < 0)
    return 0;

  if (fprintf(out, "    \"%s%s_t *\", sizeof(%s_t *), _Alignof(%s_t *),\n",
              is_const ? "const " : "", type_name, type_name, type_name) < 0)
    return 0;

  return fprintf(out,
      "    CMETA_T_POINTER, &%s_value_type, NULL, NULL\n"
      "};\n\n",
      symbol) >= 0;
}

static int plugin_operation_symbols(
    const Node *operation,
    char *business_symbol, size_t business_symbol_size,
    char *meta_symbol, size_t meta_symbol_size,
    char *request_symbol, size_t request_symbol_size,
    char *response_symbol, size_t response_symbol_size) {
  const char *native_symbol = plugin_string(operation, "native_c_symbol");
  int written;

  if (!plugin_identifier_valid(native_symbol))
    return 0;

  written = snprintf(business_symbol, business_symbol_size, "%s",
                     native_symbol);
  if (written < 0 || (size_t)written >= business_symbol_size) return 0;

  written = snprintf(meta_symbol, meta_symbol_size, "%s_plugin_meta",
                     business_symbol);
  if (written < 0 || (size_t)written >= meta_symbol_size) return 0;

  written = snprintf(request_symbol, request_symbol_size, "%s_request",
                     meta_symbol);
  if (written < 0 || (size_t)written >= request_symbol_size) return 0;

  written = snprintf(response_symbol, response_symbol_size, "%s_response",
                     meta_symbol);
  return written >= 0 && (size_t)written < response_symbol_size;
}

static int plugin_write_operation_support(
    FILE *out, const char *schema_name, const char *service_name,
    const Node *operation) {
  const char *operation_name = plugin_string(operation, "name");
  const char *request_type = plugin_string(operation, "request_type");
  const char *response_type = plugin_string(operation, "response_type");
  char symbol[PLUGIN_NATIVE_SYMBOL_MAX + 1u];
  char meta_symbol[PLUGIN_NATIVE_SYMBOL_MAX + 32u];
  char request_symbol[PLUGIN_NATIVE_SYMBOL_MAX + 48u];
  char response_symbol[PLUGIN_NATIVE_SYMBOL_MAX + 48u];

  if (!plugin_identifier_valid(request_type) ||
      !plugin_identifier_valid(response_type) ||
      strcmp(request_type, "void") == 0 ||
      strcmp(response_type, "void") == 0 ||
      !plugin_operation_symbols(
          operation,
          symbol, sizeof(symbol),
          meta_symbol, sizeof(meta_symbol),
          request_symbol, sizeof(request_symbol),
          response_symbol, sizeof(response_symbol)))
    return 0;

  if (!plugin_write_type_desc(
          out, request_symbol, schema_name, request_type, 1) ||
      !plugin_write_type_desc(
          out, response_symbol, schema_name, response_type, 0))
    return 0;

  if (fprintf(out,
      "extern int %s(const %s_t *request, %s_t *response);\n\n"
      "CMETA_FUNCTION_METADATA_AS_ABI(\n"
      "    %s, ",
      symbol, request_type, response_type, meta_symbol) < 0)
    return 0;

  {
    char display_name[520];
    int written = snprintf(display_name, sizeof(display_name), "%s.%s",
                           service_name, operation_name);
    if (written < 0 || (size_t)written >= sizeof(display_name) ||
        !plugin_write_c_string(out, display_name))
      return 0;
  }

  return fprintf(out,
      ", unknown, &cmeta_type_int, CMETA_ABI_SCALAR,\n"
      "    (const %s_t *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED, "
      "&%s_ptr_type, CMETA_ABI_POINTER),\n"
      "    (%s_t *, response, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, "
      "&%s_ptr_type, CMETA_ABI_POINTER));\n\n"
      "static bool SALTS_PLUGIN_CALL %s_invoke(\n"
      "    void *context, void *return_storage, void *const *params,\n"
      "    size_t param_count) {\n"
      "  int result;\n"
      "  if (context != NULL || return_storage == NULL || params == NULL ||\n"
      "      param_count != 2u || params[0] == NULL || params[1] == NULL)\n"
      "    return false;\n"
      "  result = %s((const %s_t *)params[0], (%s_t *)params[1]);\n"
      "  *(int *)return_storage = result;\n"
      "  return true;\n"
      "}\n\n",
      request_type, request_symbol,
      response_type, response_symbol,
      meta_symbol, symbol, request_type, response_type) >= 0;
}

static int plugin_write_export_initializer(
    FILE *out, const char *schema_name, uint32_t contract_version,
    const char *service_name, const Node *operation, int trailing_comma) {
  const char *operation_name = plugin_string(operation, "name");
  char symbol[PLUGIN_NATIVE_SYMBOL_MAX + 1u];
  char meta_symbol[PLUGIN_NATIVE_SYMBOL_MAX + 32u];
  char request_symbol[PLUGIN_NATIVE_SYMBOL_MAX + 48u];
  char response_symbol[PLUGIN_NATIVE_SYMBOL_MAX + 48u];
  char export_id[520];
  int written;

  if (!plugin_operation_symbols(
          operation,
          symbol, sizeof(symbol),
          meta_symbol, sizeof(meta_symbol),
          request_symbol, sizeof(request_symbol),
          response_symbol, sizeof(response_symbol)))
    return 0;

  (void)symbol;
  (void)request_symbol;
  (void)response_symbol;

  written = snprintf(export_id, sizeof(export_id), "%s.%s",
                     service_name, operation_name);
  if (written < 0 || (size_t)written >= sizeof(export_id)) return 0;

  if (fprintf(out,
      "  {\n"
      "    .struct_size = SALTS_PLUGIN_EXPORT_SIZE,\n"
      "    .kind = SALTS_PLUGIN_EXPORT_FUNCTION,\n"
      "    .contract_version = %uu,\n"
      "    .capabilities = 0u,\n"
      "    .export_id = ",
      (unsigned)contract_version) < 0 ||
      !plugin_write_c_string(out, export_id) ||
      fputs(",\n    .contract_id = ", out) == EOF ||
      !plugin_write_c_string(out, service_name) ||
      fprintf(out,
      ",\n"
      "    .value.function = {\n"
      "      .desc = &%s__function_meta,\n"
      "      .abi = &%s__function_abi_meta,\n"
      "      .context = NULL,\n"
      "      .invoke = %s_invoke,\n"
      "    },\n"
      "  }%s\n",
      meta_symbol, meta_symbol, meta_symbol,
      trailing_comma ? "," : "") < 0)
    return 0;

  return 1;
}

static size_t plugin_operation_count(const Node *services) {
  size_t i;
  size_t total = 0u;
  if (services == NULL || services->type != NODE_LIST) return 0u;
  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *operations =
        plugin_child(services->data.list.items[i], "operations");
    if (operations != NULL && operations->type == NODE_LIST) {
      if (SIZE_MAX - total < operations->data.list.count) return SIZE_MAX;
      total += operations->data.list.count;
    }
  }
  return total;
}

static int plugin_validate_ir(
    const Node *canonical_ir, const databind_compiler_plugin_config *config,
    const databind_compiler_projection_request *request,
    const Node **out_schema, const Node **out_services,
    const char **out_schema_name, uint32_t *out_contract_version) {
  const Node *schema;
  const Node *services;
  const char *schema_name;
  const char *schema_version;
  size_t i;
  size_t operation_count;

  if (canonical_ir == NULL || config == NULL || request == NULL ||
      request->kind != DATABIND_COMPILER_PROJECTION_PLUGIN ||
      request->output == NULL || request->output[0] == '\0' ||
      !plugin_header_path_valid(config->generated_header))
    return 0;

  schema = plugin_child(canonical_ir, "schema");
  services = plugin_child(canonical_ir, "services");
  schema_name = plugin_string(schema, "schema_name");
  schema_version = plugin_string(schema, "schema_version");
  if (!plugin_identifier_valid(schema_name) ||
      strlen(schema_name) > SALTS_PLUGIN_ID_MAX ||
      !plugin_positive_u32(schema_version, out_contract_version) ||
      services == NULL || services->type != NODE_LIST)
    return 0;

  operation_count = plugin_operation_count(services);
  if (operation_count == 0u || operation_count == SIZE_MAX)
    return 0;

  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *service = services->data.list.items[i];
    const Node *operations = plugin_child(service, "operations");
    const char *service_name = plugin_string(service, "name");
    size_t j;
    if (!plugin_identifier_valid(service_name) ||
        strlen(service_name) > SALTS_PLUGIN_CONTRACT_ID_MAX ||
        operations == NULL || operations->type != NODE_LIST ||
        operations->data.list.count == 0u)
      return 0;
    for (j = 0u; j < operations->data.list.count; ++j) {
      const Node *op = operations->data.list.items[j];
      const char *name = plugin_string(op, "name");
      const char *request_type = plugin_string(op, "request_type");
      const char *response_type = plugin_string(op, "response_type");
      size_t service_length;
      size_t operation_length;

      if (!plugin_identifier_valid(name) ||
          !plugin_identifier_valid(request_type) ||
          !plugin_identifier_valid(response_type) ||
          strcmp(request_type, "void") == 0 ||
          strcmp(response_type, "void") == 0)
        return 0;

      service_length = strlen(service_name);
      operation_length = strlen(name);
      if (service_length > SALTS_PLUGIN_EXPORT_ID_MAX - 1u ||
          operation_length >
              SALTS_PLUGIN_EXPORT_ID_MAX - service_length - 1u)
        return 0;
    }
  }

  *out_schema = schema;
  *out_services = services;
  *out_schema_name = schema_name;
  return 1;
}

int databind_compiler_plugin_generate(
    const Node *canonical_ir,
    const databind_compiler_projection_request *request,
    void *context) {
  const databind_compiler_plugin_config *config =
      (const databind_compiler_plugin_config *)context;
  const Node *schema = NULL;
  const Node *services = NULL;
  const char *schema_name = NULL;
  uint32_t contract_version = 0u;
  size_t operation_count;
  size_t emitted = 0u;
  size_t i;
  FILE *out = NULL;
  char *temporary_output = NULL;
  int output_open = 0;
  int ok = 0;

  if (!plugin_validate_ir(
          canonical_ir, config, request, &schema, &services,
          &schema_name, &contract_version))
    return -1;

  (void)schema;
  operation_count = plugin_operation_count(services);

  if (!plugin_create_temporary_output(
          request->output, &temporary_output, &out))
    return -1;
  output_open = 1;

  if (fputs(
      "/* Generated by databindc PLUGIN projection. */\n"
      "#include <salts/plugin.h>\n"
      "#include <cmeta/function.h>\n"
      "#include <stdbool.h>\n"
      "#include <stddef.h>\n"
      "#include <stdint.h>\n"
      "#include ", out) == EOF ||
      !plugin_write_c_string(out, config->generated_header) ||
      fputs("\n\n", out) == EOF)
    goto cleanup;

  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *service = services->data.list.items[i];
    const Node *operations = plugin_child(service, "operations");
    const char *service_name = plugin_string(service, "name");
    size_t j;
    for (j = 0u; j < operations->data.list.count; ++j) {
      if (!plugin_write_operation_support(
              out, schema_name, service_name,
              operations->data.list.items[j]))
        goto cleanup;
    }
  }

  if (fprintf(out,
      "static const salts_plugin_export plugin_exports[%zu] = {\n",
      operation_count) < 0)
    goto cleanup;

  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *service = services->data.list.items[i];
    const Node *operations = plugin_child(service, "operations");
    const char *service_name = plugin_string(service, "name");
    size_t j;
    for (j = 0u; j < operations->data.list.count; ++j) {
      ++emitted;
      if (!plugin_write_export_initializer(
              out, schema_name, contract_version, service_name,
              operations->data.list.items[j], emitted != operation_count))
        goto cleanup;
    }
  }
  if (emitted != operation_count || fputs("};\n\n", out) == EOF)
    goto cleanup;

  if (fputs(
      "static const salts_plugin_manifest plugin_manifest = {\n"
      "  .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,\n"
      "  .abi_version = SALTS_PLUGIN_ABI_VERSION,\n"
      "  .plugin_id = ", out) == EOF ||
      !plugin_write_c_string(out, schema_name) ||
      fprintf(out,
      ",\n"
      "  .version = {%uu, %uu, %uu},\n"
      "  .exports = plugin_exports,\n"
      "  .export_count = %zuu,\n"
      "};\n\n"
      "SALTS_PLUGIN_QUERY_EXPORT\n"
      "const salts_plugin_manifest *SALTS_PLUGIN_CALL\n"
      "salts_plugin_query(uint32_t host_abi) {\n"
      "  return host_abi == SALTS_PLUGIN_ABI_VERSION ? &plugin_manifest : NULL;\n"
      "}\n",
      (unsigned)config->artifact_version_major,
      (unsigned)config->artifact_version_minor,
      (unsigned)config->artifact_version_patch,
      operation_count) < 0)
    goto cleanup;

  if (fflush(out) != 0 || ferror(out)) goto cleanup;
  if (fclose(out) != 0) {
    out = NULL;
    output_open = 0;
    goto cleanup;
  }
  out = NULL;
  output_open = 0;

  if (salts_fs_rename(temporary_output, request->output) != 0)
    goto cleanup;

  ok = 1;

cleanup:
  if (output_open && out != NULL) (void)fclose(out);
  if (!ok && temporary_output != NULL)
    (void)salts_fs_unlink(temporary_output);
  free(temporary_output);
  return ok ? 0 : -1;
}

databind_compiler_projection_backend
databind_compiler_plugin_backend(
    const databind_compiler_plugin_config *config) {
  databind_compiler_projection_backend backend;
  backend.kind = DATABIND_COMPILER_PROJECTION_PLUGIN;
  backend.name = "plugin";
  backend.generate = databind_compiler_plugin_generate;
  backend.context = (void *)config;
  return backend;
}

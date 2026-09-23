#include "plugin_projection.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int plugin_write_operation(
    FILE *out, const char *schema_name, uint32_t contract_version,
    const char *service_name, const Node *operation, size_t export_index) {
  const char *operation_name = plugin_string(operation, "name");
  const char *request_type = plugin_string(operation, "request_type");
  const char *response_type = plugin_string(operation, "response_type");
  char symbol[512];
  char meta_symbol[560];
  char request_symbol[560];
  char response_symbol[560];

  if (!plugin_identifier_valid(service_name) ||
      !plugin_identifier_valid(operation_name) ||
      !plugin_identifier_valid(request_type) ||
      !plugin_identifier_valid(response_type) ||
      strcmp(request_type, "void") == 0 ||
      strcmp(response_type, "void") == 0)
    return 0;

  if (snprintf(symbol, sizeof(symbol), "%s_%s_%s",
               schema_name, service_name, operation_name) < 0 ||
      strlen(symbol) >= sizeof(symbol))
    return 0;
  if (snprintf(meta_symbol, sizeof(meta_symbol), "%s_plugin_meta", symbol) < 0 ||
      snprintf(request_symbol, sizeof(request_symbol), "%s_request", meta_symbol) < 0 ||
      snprintf(response_symbol, sizeof(response_symbol), "%s_response", meta_symbol) < 0)
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
    if (snprintf(display_name, sizeof(display_name), "%s.%s",
                 service_name, operation_name) < 0 ||
        strlen(display_name) >= sizeof(display_name) ||
        !plugin_write_c_string(out, display_name))
      return 0;
  }

  if (fprintf(out,
      ", value, &cmeta_type_int, CMETA_ABI_SCALAR,\n"
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
      meta_symbol, symbol, request_type, response_type) < 0)
    return 0;

  if (fprintf(out,
      "static const salts_plugin_export plugin_export_%zu = {\n"
      "  .struct_size = SALTS_PLUGIN_EXPORT_SIZE,\n"
      "  .kind = SALTS_PLUGIN_EXPORT_FUNCTION,\n"
      "  .contract_version = %uu,\n"
      "  .capabilities = 0u,\n"
      "  .export_id = ",
      export_index, (unsigned)contract_version) < 0)
    return 0;

  {
    char export_id[520];
    if (snprintf(export_id, sizeof(export_id), "%s.%s",
                 service_name, operation_name) < 0 ||
        strlen(export_id) >= sizeof(export_id) ||
        !plugin_write_c_string(out, export_id))
      return 0;
  }

  if (fputs(",\n  .contract_id = ", out) == EOF ||
      !plugin_write_c_string(out, service_name) ||
      fprintf(out,
      ",\n"
      "  .value.function = {\n"
      "    .desc = &%s__function_meta,\n"
      "    .abi = &%s__function_abi_meta,\n"
      "    .context = NULL,\n"
      "    .invoke = %s_invoke,\n"
      "  },\n"
      "};\n\n",
      meta_symbol, meta_symbol, meta_symbol) < 0)
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
      config->generated_header == NULL || config->generated_header[0] == '\0')
    return 0;

  schema = plugin_child(canonical_ir, "schema");
  services = plugin_child(canonical_ir, "services");
  schema_name = plugin_string(schema, "schema_name");
  schema_version = plugin_string(schema, "schema_version");
  if (!plugin_identifier_valid(schema_name) ||
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
        operations == NULL || operations->type != NODE_LIST ||
        operations->data.list.count == 0u)
      return 0;
    for (j = 0u; j < operations->data.list.count; ++j) {
      const Node *op = operations->data.list.items[j];
      const char *name = plugin_string(op, "name");
      const char *request_type = plugin_string(op, "request_type");
      const char *response_type = plugin_string(op, "response_type");
      if (!plugin_identifier_valid(name) ||
          !plugin_identifier_valid(request_type) ||
          !plugin_identifier_valid(response_type) ||
          strcmp(request_type, "void") == 0 ||
          strcmp(response_type, "void") == 0)
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
  size_t export_index = 0u;
  size_t i;
  FILE *out = NULL;
  int ok = 0;

  if (!plugin_validate_ir(
          canonical_ir, config, request, &schema, &services,
          &schema_name, &contract_version))
    return -1;

  (void)schema;
  operation_count = plugin_operation_count(services);

  out = fopen(request->output, "wb");
  if (out == NULL) return -1;

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
      if (!plugin_write_operation(
              out, schema_name, contract_version, service_name,
              operations->data.list.items[j], export_index))
        goto cleanup;
      ++export_index;
    }
  }

  if (fprintf(out,
      "static const salts_plugin_export *const plugin_export_ptrs[%zu] = {\n",
      operation_count) < 0)
    goto cleanup;
  for (i = 0u; i < operation_count; ++i)
    if (fprintf(out, "  &plugin_export_%zu%s\n", i,
                i + 1u == operation_count ? "" : ",") < 0)
      goto cleanup;
  if (fputs("};\n\n", out) == EOF) goto cleanup;

  /*
   * Manifest requires a contiguous export array. Copy the generated immutable
   * rows into one array at translation time rather than publishing pointers.
   */
  if (fprintf(out,
      "static const salts_plugin_export plugin_exports[%zu] = {\n",
      operation_count) < 0)
    goto cleanup;
  for (i = 0u; i < operation_count; ++i)
    if (fprintf(out, "  plugin_export_%zu%s\n", i,
                i + 1u == operation_count ? "" : ",") < 0)
      goto cleanup;
  if (fputs("};\n\n", out) == EOF) goto cleanup;

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
  ok = 1;

cleanup:
  if (fclose(out) != 0) ok = 0;
  if (!ok) (void)remove(request->output);
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

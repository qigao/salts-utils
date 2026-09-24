#include "plugin_projection.h"

#include "service_native.h"
#include "salts_fs.h"

#include <salts/plugin.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Node *plugin_child(const Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP || name == NULL) return NULL;
  for (i = 0u; i < parent->data.map.count; ++i) {
    const Node *child = parent->data.map.items[i];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static const Node *plugin_list(const Node *parent, const char *name) {
  const Node *child = plugin_child(parent, name);
  return child != NULL && child->type == NODE_LIST ? child : NULL;
}

static const char *plugin_string(const Node *parent, const char *name) {
  const Node *child = plugin_child(parent, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

static const char *plugin_schema_name(const Node *root) {
  return plugin_string(plugin_child(root, "schema"), "schema_name");
}

static int plugin_schema_contract_version(
    const Node *root, uint32_t *out_version) {
  const Node *schema = plugin_child(root, "schema");
  const char *text = plugin_string(schema, "schema_version");
  const unsigned char *p;
  uint64_t value = 0u;

  if (out_version == NULL || text == NULL || text[0] == '\0') return 0;

  for (p = (const unsigned char *)text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return 0;
    value = value * 10u + (uint64_t)(*p - '0');
    if (value > UINT32_MAX) return 0;
  }

  if (value == 0u) return 0;
  *out_version = (uint32_t)value;
  return 1;
}

static int plugin_text_valid(const char *text) {
  return text != NULL && text[0] != '\0';
}

static int plugin_bounded_text_valid(const char *text, size_t max_length) {
  return plugin_text_valid(text) && strlen(text) <= max_length;
}

static const Node *plugin_component(
    const Node *root, const char *component_id) {
  const Node *components = plugin_list(root, "components");
  size_t i;

  if (components == NULL || component_id == NULL) return NULL;
  for (i = 0u; i < components->data.list.count; ++i) {
    const Node *qualified_name =
        plugin_child(components->data.list.items[i], "qualified_name");
    if (qualified_name != NULL &&
        qualified_name->type == NODE_STRING &&
        qualified_name->data.string_val != NULL &&
        strcmp(qualified_name->data.string_val, component_id) == 0)
      return components->data.list.items[i];
  }
  return NULL;
}

static const Node *plugin_component_capabilities(
    const Node *component) {
  return plugin_list(component, "capabilities");
}

static int plugin_component_has_service(
    const Node *component, const char *service_name) {
  const Node *capabilities =
      plugin_component_capabilities(component);
  size_t i;

  if (capabilities == NULL || service_name == NULL) return 0;
  for (i = 0u; i < capabilities->data.list.count; ++i) {
    const Node *capability = capabilities->data.list.items[i];
    const char *kind = plugin_string(capability, "kind");
    const char *name = plugin_string(capability, "name");
    if (kind != NULL && name != NULL &&
        strcmp(kind, "service") == 0 &&
        strcmp(name, service_name) == 0)
      return 1;
  }
  return 0;
}

static const Node *plugin_service(
    const Node *root, const char *service_name) {
  const Node *services = plugin_list(root, "services");
  size_t i;

  if (services == NULL || service_name == NULL) return NULL;
  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *service = services->data.list.items[i];
    const char *name = plugin_string(service, "name");
    if (name != NULL && strcmp(name, service_name) == 0)
      return service;
  }
  return NULL;
}

static int plugin_selected_typed_errors_absent(
    const Node *root, const Node *component) {
  const Node *capabilities =
      plugin_component_capabilities(component);
  size_t i;

  if (capabilities == NULL) return 0;

  for (i = 0u; i < capabilities->data.list.count; ++i) {
    const Node *capability = capabilities->data.list.items[i];
    const char *kind = plugin_string(capability, "kind");
    const char *service_name = plugin_string(capability, "name");
    const Node *service;
    const Node *operations;
    size_t j;

    if (kind == NULL || strcmp(kind, "service") != 0)
      continue;

    service = plugin_service(root, service_name);
    operations = plugin_list(service, "operations");
    if (operations == NULL || operations->data.list.count == 0u)
      return 0;

    for (j = 0u; j < operations->data.list.count; ++j) {
      const Node *errors =
          plugin_list(operations->data.list.items[j], "errors");
      if (errors != NULL && errors->data.list.count != 0u)
        return 0;
    }
  }

  return 1;
}

static int plugin_operation_selected(
    const Node *component,
    const databind_compiler_service_native_operation *operation) {
  return operation != NULL &&
         plugin_component_has_service(
             component, operation->service_name);
}

static int plugin_select_component_service(
    void *context, const char *service_name) {
  return plugin_component_has_service(
      (const Node *)context, service_name);
}

static int plugin_native_ir_valid(
    const Node *root,
    const Node *component,
    const databind_compiler_service_native_ir *ir,
    size_t *out_selected_count) {
  const char *plugin_id =
      plugin_string(component, "qualified_name");
  size_t selected_count = 0u;
  size_t i;

  if (out_selected_count != NULL) *out_selected_count = 0u;

  if (!plugin_bounded_text_valid(plugin_id, SALTS_PLUGIN_ID_MAX) ||
      ir == NULL || ir->operations == NULL ||
      !plugin_selected_typed_errors_absent(root, component))
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (!plugin_operation_selected(component, operation))
      continue;

    if (!plugin_bounded_text_valid(
            operation->qualified_service,
            SALTS_PLUGIN_CONTRACT_ID_MAX) ||
        !plugin_bounded_text_valid(
            operation->qualified_operation,
            SALTS_PLUGIN_EXPORT_ID_MAX) ||
        !plugin_text_valid(operation->symbol) ||
        !plugin_text_valid(operation->request_type) ||
        !plugin_text_valid(operation->response_type))
      return 0;

    ++selected_count;
    if (selected_count > SALTS_PLUGIN_MAX_EXPORTS)
      return 0;
  }

  if (selected_count == 0u) return 0;
  if (out_selected_count != NULL)
    *out_selected_count = selected_count;
  return 1;
}

static int plugin_header_guard(
    const char *schema_name, const char *component_name,
    char *out, size_t out_size) {
  static const char suffix[] = "_PLUGIN_SERVICE_H";
  const char *parts[] = {schema_name, component_name};
  size_t used = 0u;
  size_t part;

  if (!plugin_text_valid(schema_name) ||
      !plugin_text_valid(component_name) ||
      out == NULL || out_size == 0u)
    return 0;

  for (part = 0u; part < 2u; ++part) {
    const char *text = parts[part];
    size_t i;
    if (part != 0u) {
      if (used + 1u >= out_size) return 0;
      out[used++] = '_';
    }
    for (i = 0u; text[i] != '\0'; ++i) {
      unsigned char ch = (unsigned char)text[i];
      if (!((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_'))
        return 0;
      if (used + 1u >= out_size) return 0;
      out[used++] =
          ch >= 'a' && ch <= 'z'
              ? (char)(ch - 'a' + 'A')
              : (char)ch;
    }
  }

  if (used > SIZE_MAX - sizeof(suffix) ||
      used + sizeof(suffix) > out_size)
    return 0;

  memcpy(out + used, suffix, sizeof(suffix));
  return 1;
}

static const char *plugin_basename(const char *path) {
  const char *base = path;
  const char *p;
  if (path == NULL) return NULL;
  for (p = path; *p != '\0'; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;
  return base;
}

static int plugin_write_c_string(FILE *file, const char *text) {
  const unsigned char *p = (const unsigned char *)text;

  if (file == NULL || text == NULL || fputc('"', file) == EOF) return 0;

  for (; *p != '\0'; ++p) {
    switch (*p) {
      case '\\':
        if (fputs("\\\\", file) == EOF) return 0;
        break;
      case '"':
        if (fputs("\\\"", file) == EOF) return 0;
        break;
      case '\n':
        if (fputs("\\n", file) == EOF) return 0;
        break;
      case '\r':
        if (fputs("\\r", file) == EOF) return 0;
        break;
      case '\t':
        if (fputs("\\t", file) == EOF) return 0;
        break;
      default:
        if (*p < 0x20u || *p == 0x7fu) return 0;
        if (fputc((int)*p, file) == EOF) return 0;
        break;
    }
  }

  return fputc('"', file) != EOF;
}

static char *plugin_suffixed_path(
    const char *path, const char *suffix) {
  size_t path_length;
  size_t suffix_length;
  char *out;

  if (path == NULL || suffix == NULL) return NULL;

  path_length = strlen(path);
  suffix_length = strlen(suffix);
  if (path_length > SIZE_MAX - suffix_length - 1u) return NULL;

  out = (char *)malloc(path_length + suffix_length + 1u);
  if (out == NULL) return NULL;

  memcpy(out, path, path_length);
  memcpy(out + path_length, suffix, suffix_length + 1u);
  return out;
}

static void plugin_unlink_if_exists(const char *path) {
  if (path != NULL &&
      salts_fs_access(path, SALTS_FS_ACCESS_EXISTS) == 0)
    (void)salts_fs_unlink(path);
}

static FILE *plugin_open_staging(
    const char *final_path, char **out_staging_path) {
  char *staging;
  FILE *file;

  if (out_staging_path == NULL) return NULL;
  *out_staging_path = NULL;

  staging = plugin_suffixed_path(final_path, ".databind-plugin.tmp");
  if (staging == NULL) return NULL;

  plugin_unlink_if_exists(staging);
  file = fopen(staging, "wb");
  if (file == NULL) {
    free(staging);
    return NULL;
  }

  *out_staging_path = staging;
  return file;
}

static int plugin_close_staging(FILE **file) {
  int ok = 1;

  if (file == NULL || *file == NULL) return 0;
  if (fflush(*file) != 0) ok = 0;
  if (fclose(*file) != 0) ok = 0;
  *file = NULL;
  return ok;
}

typedef struct plugin_output_transaction {
  const char *final_path;
  char *staging_path;
  char *backup_path;
  int had_original;
  int published;
} plugin_output_transaction;

static int plugin_prepare_output_backup(
    plugin_output_transaction *output) {
  if (output == NULL || output->final_path == NULL ||
      output->staging_path == NULL)
    return 0;

  output->backup_path =
      plugin_suffixed_path(output->final_path, ".databind-plugin.bak");
  if (output->backup_path == NULL) return 0;

  plugin_unlink_if_exists(output->backup_path);

  if (salts_fs_access(
          output->final_path, SALTS_FS_ACCESS_EXISTS) == 0) {
    if (salts_fs_rename(
            output->final_path, output->backup_path) != 0)
      return 0;
    output->had_original = 1;
  }

  return 1;
}

static void plugin_rollback_output(
    plugin_output_transaction *output) {
  if (output == NULL) return;

  if (output->published)
    plugin_unlink_if_exists(output->final_path);

  if (output->had_original && output->backup_path != NULL)
    (void)salts_fs_rename(
        output->backup_path, output->final_path);
  else if (output->backup_path != NULL)
    plugin_unlink_if_exists(output->backup_path);

  if (output->staging_path != NULL)
    plugin_unlink_if_exists(output->staging_path);
}

static void plugin_free_output_paths(
    plugin_output_transaction *output) {
  if (output == NULL) return;
  free(output->staging_path);
  free(output->backup_path);
  output->staging_path = NULL;
  output->backup_path = NULL;
}

static int plugin_commit_output_set(
    const char *const *final_paths,
    char **staging_paths,
    size_t count) {
  plugin_output_transaction *outputs = NULL;
  size_t i;
  int ok = 0;

  if (final_paths == NULL || staging_paths == NULL || count == 0u)
    return 0;
  if (count > SIZE_MAX / sizeof(*outputs)) return 0;

  outputs = (plugin_output_transaction *)calloc(
      count, sizeof(*outputs));
  if (outputs == NULL) return 0;

  for (i = 0u; i < count; ++i) {
    if (final_paths[i] == NULL || staging_paths[i] == NULL)
      goto rollback;
    outputs[i].final_path = final_paths[i];
    outputs[i].staging_path = staging_paths[i];
  }

  for (i = 0u; i < count; ++i)
    if (!plugin_prepare_output_backup(&outputs[i]))
      goto rollback;

  for (i = 0u; i < count; ++i) {
    if (salts_fs_rename(
            outputs[i].staging_path,
            outputs[i].final_path) != 0)
      goto rollback;
    outputs[i].published = 1;
  }

  for (i = 0u; i < count; ++i)
    if (outputs[i].had_original)
      plugin_unlink_if_exists(outputs[i].backup_path);

  ok = 1;
  goto cleanup;

rollback:
  for (i = count; i > 0u; --i)
    plugin_rollback_output(&outputs[i - 1u]);

cleanup:
  for (i = 0u; i < count; ++i) {
    plugin_free_output_paths(&outputs[i]);
    staging_paths[i] = NULL;
  }
  free(outputs);
  return ok;
}

static char *plugin_component_symbol(
    const Node *root, const Node *component) {
  const char *parts[] = {
      plugin_schema_name(root),
      plugin_string(component, "name")
  };
  return databind_compiler_native_symbol(
      parts, sizeof(parts) / sizeof(parts[0]));
}

static char *plugin_client_call_symbol(
    const Node *root,
    const Node *component,
    const databind_compiler_service_native_operation *operation) {
  const char *parts[] = {
      plugin_schema_name(root),
      plugin_string(component, "name"),
      operation != NULL ? operation->service_name : NULL,
      operation != NULL ? operation->operation_name : NULL
  };
  char *base;
  char *out;
  size_t length;
  static const char suffix[] = "_plugin_call";

  base = databind_compiler_native_symbol(
      parts, sizeof(parts) / sizeof(parts[0]));
  if (base == NULL) return NULL;

  length = strlen(base);
  if (length > SIZE_MAX - sizeof(suffix)) {
    free(base);
    return NULL;
  }

  out = (char *)malloc(length + sizeof(suffix));
  if (out != NULL) {
    memcpy(out, base, length);
    memcpy(out + length, suffix, sizeof(suffix));
  }
  free(base);
  return out;
}

static int plugin_write_header(
    FILE *file,
    const Node *root,
    const Node *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir) {
  char guard[320];
  char *component_symbol = NULL;
  size_t i;
  int ok = 0;

  if (!plugin_header_guard(
          plugin_schema_name(root),
          plugin_string(component, "name"),
          guard, sizeof(guard)))
    return 0;

  component_symbol = plugin_component_symbol(root, component);
  if (component_symbol == NULL) return 0;

  if (fprintf(file, "#ifndef %s\n#define %s\n\n", guard, guard) < 0 ||
      fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, config->native_header) ||
      fputs(
          "\n#include <salts/plugin.h>\n"
          "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n",
          file) == EOF)
    goto cleanup;

  for (i = 0u; i < ir->operation_count; ++i)
    if (databind_compiler_service_native_emit_prototype(
            file, &ir->operations[i]) != 0)
      goto cleanup;

  if (fprintf(
          file,
          "\ntypedef struct %s_plugin_client {\n"
          "  salts_plugin_registry *registry;\n"
          "  salts_plugin_lease lease;\n"
          "  const salts_plugin_export *exports[%zu];\n"
          "} %s_plugin_client;\n"
          "#define %s_PLUGIN_CLIENT_INIT {0}\n\n"
          "salts_plugin_status %s_plugin_client_open(\n"
          "    salts_plugin_registry *registry, salts_plugin_ref plugin,\n"
          "    %s_plugin_client *out_client);\n"
          "salts_plugin_status %s_plugin_client_close(\n"
          "    %s_plugin_client *client);\n\n",
          component_symbol, ir->operation_count,
          component_symbol,
          component_symbol,
          component_symbol, component_symbol,
          component_symbol, component_symbol) < 0)
    goto cleanup;

  for (i = 0u; i < ir->operation_count; ++i) {
    char *call_symbol =
        plugin_client_call_symbol(root, component, &ir->operations[i]);
    if (call_symbol == NULL) goto cleanup;
    if (fprintf(
            file,
            "bool %s(\n"
            "    %s_plugin_client *client,\n"
            "    const %s_t *request, %s_t *response,\n"
            "    int *native_status);\n",
            call_symbol, component_symbol,
            ir->operations[i].request_type,
            ir->operations[i].response_type) < 0) {
      free(call_symbol);
      goto cleanup;
    }
    free(call_symbol);
  }

  if (fprintf(
          file,
          "\n#ifdef __cplusplus\n}\n#endif\n\n"
          "#endif /* %s */\n",
          guard) < 0)
    goto cleanup;

  ok = 1;

cleanup:
  free(component_symbol);
  return ok;
}

static int plugin_write_adapter(
    FILE *file,
    const databind_compiler_service_native_operation *operation) {
  if (file == NULL || operation == NULL ||
      !plugin_text_valid(operation->symbol) ||
      !plugin_text_valid(operation->request_type) ||
      !plugin_text_valid(operation->response_type))
    return 0;

  return fprintf(
             file,
             "static bool SALTS_PLUGIN_CALL %s__plugin_invoke(\n"
             "    void *context, void *return_storage, void *const *params,\n"
             "    size_t param_count) {\n"
             "  int result;\n"
             "  (void)context;\n"
             "  if (return_storage == NULL || params == NULL ||\n"
             "      param_count != 2u || params[0] == NULL || params[1] == NULL)\n"
             "    return false;\n"
             "  result = %s((const %s_t *)params[0], (%s_t *)params[1]);\n"
             "  *(int *)return_storage = result;\n"
             "  return true;\n"
             "}\n\n",
             operation->symbol,
             operation->symbol,
             operation->request_type,
             operation->response_type) >= 0;
}

static int plugin_write_source(
    FILE *file,
    const Node *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir,
    uint32_t contract_version) {
  const char *service_header =
      plugin_basename(config->service_header_output);
  const char *plugin_id =
      plugin_string(component, "qualified_name");
  size_t i;

  if (fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, service_header) ||
      fputs(
          "\n#include <salts/plugin.h>\n"
          "#include <cmeta/function.h>\n\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, &ir->operations[i], 0) != 0 ||
        fputc('\n', file) == EOF ||
        !plugin_write_adapter(file, &ir->operations[i]))
      return 0;
  }

  if (fputs(
          "static const salts_plugin_export databind_plugin_exports[] = {\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (fputs(
            "  {\n"
            "    .struct_size = SALTS_PLUGIN_EXPORT_SIZE,\n"
            "    .kind = SALTS_PLUGIN_EXPORT_FUNCTION,\n",
            file) == EOF ||
        fprintf(
            file,
            "    .contract_version = %uu,\n"
            "    .capabilities = 0u,\n"
            "    .export_id = ",
            contract_version) < 0 ||
        !plugin_write_c_string(file, operation->qualified_operation) ||
        fputs(",\n    .contract_id = ", file) == EOF ||
        !plugin_write_c_string(file, operation->qualified_service) ||
        fprintf(
            file,
            ",\n"
            "    .value.function = {\n"
            "      .desc = &%s__function_meta,\n"
            "      .abi = &%s__function_abi_meta,\n"
            "      .context = NULL,\n"
            "      .invoke = %s__plugin_invoke,\n"
            "    },\n"
            "  },\n",
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return 0;
  }

  if (fputs(
          "};\n\n"
          "static const salts_plugin_manifest databind_plugin_manifest = {\n"
          "  .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,\n"
          "  .abi_version = SALTS_PLUGIN_ABI_VERSION,\n"
          "  .plugin_id = ",
          file) == EOF ||
      !plugin_write_c_string(file, plugin_id) ||
      fprintf(
          file,
          ",\n"
          "  .version = {%uu, %uu, %uu},\n"
          "  .exports = databind_plugin_exports,\n"
          "  .export_count = %zuu,\n"
          "};\n\n"
          "SALTS_PLUGIN_QUERY_EXPORT\n"
          "const salts_plugin_manifest *SALTS_PLUGIN_CALL\n"
          "salts_plugin_query(uint32_t host_abi) {\n"
          "  return host_abi == SALTS_PLUGIN_ABI_VERSION\n"
          "             ? &databind_plugin_manifest\n"
          "             : NULL;\n"
          "}\n",
          config->plugin_version_major,
          config->plugin_version_minor,
          config->plugin_version_patch,
          ir->operation_count) < 0)
    return 0;

  return 1;
}

static int plugin_write_client_source(
    FILE *file,
    const Node *root,
    const Node *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir,
    uint32_t contract_version) {
  const char *service_header =
      plugin_basename(config->service_header_output);
  const char *plugin_id =
      plugin_string(component, "qualified_name");
  char *component_symbol = NULL;
  size_t i;
  int ok = 0;

  if (file == NULL || !plugin_text_valid(service_header) ||
      !plugin_text_valid(plugin_id) ||
      ir == NULL || ir->operations == NULL ||
      ir->operation_count == 0u)
    return 0;

  component_symbol = plugin_component_symbol(root, component);
  if (component_symbol == NULL) return 0;

  if (fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, service_header) ||
      fputs(
          "\n#include <cmeta/function.h>\n"
          "#include <string.h>\n\n",
          file) == EOF)
    goto cleanup;

  for (i = 0u; i < ir->operation_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, &ir->operations[i], 0) != 0 ||
        fputc('\n', file) == EOF)
      goto cleanup;
  }

  if (fprintf(
          file,
          "static salts_plugin_status %s__plugin_client_fail(\n"
          "    salts_plugin_registry *registry,\n"
          "    salts_plugin_lease *lease,\n"
          "    salts_plugin_status status) {\n"
          "  salts_plugin_status release_status;\n"
          "  if (registry == NULL || lease == NULL ||\n"
          "      !salts_plugin_lease_valid(*lease))\n"
          "    return status;\n"
          "  release_status = salts_plugin_registry_release(registry, lease);\n"
          "  return release_status == SALTS_PLUGIN_OK\n"
          "             ? status\n"
          "             : release_status;\n"
          "}\n\n"
          "salts_plugin_status %s_plugin_client_open(\n"
          "    salts_plugin_registry *registry, salts_plugin_ref plugin,\n"
          "    %s_plugin_client *out_client) {\n"
          "  salts_plugin_lease lease = {0};\n"
          "  const salts_plugin_manifest *manifest = NULL;\n"
          "  const salts_plugin_export *resolved[%zu] = {0};\n"
          "  salts_plugin_status status;\n"
          "  if (registry == NULL || out_client == NULL ||\n"
          "      !salts_plugin_ref_valid(plugin))\n"
          "    return SALTS_PLUGIN_INVALID_ARGUMENT;\n"
          "  if (out_client->registry != NULL ||\n"
          "      salts_plugin_lease_valid(out_client->lease))\n"
          "    return SALTS_PLUGIN_ALREADY;\n"
          "  memset(out_client, 0, sizeof(*out_client));\n"
          "  status = salts_plugin_registry_acquire(\n"
          "      registry, plugin, &lease, &manifest);\n"
          "  if (status != SALTS_PLUGIN_OK) return status;\n"
          "  if (manifest == NULL || manifest->plugin_id == NULL ||\n"
          "      strcmp(manifest->plugin_id, ",
          component_symbol,
          component_symbol,
          component_symbol,
          ir->operation_count) < 0 ||
      !plugin_write_c_string(file, plugin_id) ||
      fputs(
          ") != 0) {\n"
          "    status = SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
          "    goto fail;\n"
          "  }\n",
          file) == EOF)
    goto cleanup;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (fputs(
            "  status = salts_plugin_manifest_find_export(\n"
            "      manifest, ",
            file) == EOF ||
        !plugin_write_c_string(file, operation->qualified_operation) ||
        fprintf(
            file,
            ", &resolved[%zu]);\n"
            "  if (status != SALTS_PLUGIN_OK) goto fail;\n"
            "  status = salts_plugin_export_require_function(\n"
            "      resolved[%zu], ",
            i, i) < 0 ||
        !plugin_write_c_string(file, operation->qualified_service) ||
        fprintf(
            file,
            ", %uu, 0u);\n"
            "  if (status != SALTS_PLUGIN_OK) goto fail;\n"
            "  if (!cmeta_function_desc_equal(\n"
            "          resolved[%zu]->value.function.desc,\n"
            "          &%s__function_meta) ||\n"
            "      !cmeta_function_abi_desc_equal(\n"
            "          resolved[%zu]->value.function.abi,\n"
            "          &%s__function_abi_meta)) {\n"
            "    status = SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
            "    goto fail;\n"
            "  }\n",
            contract_version,
            i, operation->symbol,
            i, operation->symbol) < 0)
      goto cleanup;
  }

  if (fprintf(
          file,
          "  out_client->registry = registry;\n"
          "  out_client->lease = lease;\n") < 0)
    goto cleanup;

  for (i = 0u; i < ir->operation_count; ++i)
    if (fprintf(
            file,
            "  out_client->exports[%zu] = resolved[%zu];\n",
            i, i) < 0)
      goto cleanup;

  if (fprintf(
          file,
          "  return SALTS_PLUGIN_OK;\n"
          "fail:\n"
          "  return %s__plugin_client_fail(\n"
          "      registry, &lease, status);\n"
          "}\n\n"
          "salts_plugin_status %s_plugin_client_close(\n"
          "    %s_plugin_client *client) {\n"
          "  salts_plugin_status status;\n"
          "  if (client == NULL) return SALTS_PLUGIN_INVALID_ARGUMENT;\n"
          "  if (client->registry == NULL ||\n"
          "      !salts_plugin_lease_valid(client->lease))\n"
          "    return SALTS_PLUGIN_INVALID_STATE;\n"
          "  status = salts_plugin_registry_release(\n"
          "      client->registry, &client->lease);\n"
          "  if (status == SALTS_PLUGIN_OK)\n"
          "    memset(client, 0, sizeof(*client));\n"
          "  return status;\n"
          "}\n\n",
          component_symbol,
          component_symbol,
          component_symbol) < 0)
    goto cleanup;

  for (i = 0u; i < ir->operation_count; ++i) {
    char *call_symbol =
        plugin_client_call_symbol(root, component, &ir->operations[i]);
    if (call_symbol == NULL) goto cleanup;

    if (fprintf(
            file,
            "bool %s(\n"
            "    %s_plugin_client *client,\n"
            "    const %s_t *request, %s_t *response,\n"
            "    int *native_status) {\n"
            "  void *params[2];\n"
            "  const salts_plugin_export *entry;\n"
            "  if (client == NULL || request == NULL || response == NULL ||\n"
            "      native_status == NULL || client->registry == NULL ||\n"
            "      !salts_plugin_lease_valid(client->lease))\n"
            "    return false;\n"
            "  entry = client->exports[%zu];\n"
            "  if (entry == NULL) return false;\n"
            "  params[0] = (void *)(const void *)request;\n"
            "  params[1] = response;\n"
            "  return entry->value.function.invoke(\n"
            "      entry->value.function.context, native_status, params, 2u);\n"
            "}\n\n",
            call_symbol,
            component_symbol,
            ir->operations[i].request_type,
            ir->operations[i].response_type,
            i) < 0) {
      free(call_symbol);
      goto cleanup;
    }
    free(call_symbol);
  }

  ok = 1;

cleanup:
  free(component_symbol);
  return ok;
}

int databind_compiler_plugin_generate(
    const Node *canonical_ir,
    const databind_compiler_projection_request *request,
    void *context) {
  const databind_compiler_plugin_config *config =
      request != NULL
          ? (const databind_compiler_plugin_config *)request->config
          : NULL;
  databind_compiler_service_native_ir native_ir = {0};
  const Node *component = NULL;
  size_t selected_count = 0u;
  uint32_t contract_version = 0u;
  char *header_staging = NULL;
  char *source_staging = NULL;
  char *client_staging = NULL;
  FILE *header_file = NULL;
  FILE *source_file = NULL;
  FILE *client_file = NULL;
  int result = -1;
  (void)context;

  if (canonical_ir == NULL || request == NULL ||
      request->kind != DATABIND_COMPILER_PROJECTION_PLUGIN ||
      config == NULL ||
      !plugin_text_valid(request->output) ||
      !plugin_text_valid(config->component_id) ||
      !plugin_text_valid(config->native_header) ||
      !plugin_text_valid(config->service_header_output) ||
      !plugin_text_valid(config->client_source_output) ||
      strcmp(request->output, config->service_header_output) == 0 ||
      strcmp(request->output, config->client_source_output) == 0 ||
      strcmp(config->service_header_output, config->client_source_output) == 0 ||
      !plugin_schema_contract_version(
          canonical_ir, &contract_version))
    return -1;

  component = plugin_component(
      canonical_ir, config->component_id);
  if (component == NULL)
    return -1;

  if (databind_compiler_service_native_build_selected(
          canonical_ir,
          plugin_select_component_service,
          (void *)component,
          &native_ir) != 0)
    goto cleanup;

  if (!plugin_native_ir_valid(
          canonical_ir, component, &native_ir,
          &selected_count) ||
      selected_count != native_ir.operation_count)
    goto cleanup;

  header_file = plugin_open_staging(
      config->service_header_output, &header_staging);
  if (header_file == NULL) goto cleanup;

  source_file = plugin_open_staging(
      request->output, &source_staging);
  if (source_file == NULL) goto cleanup;

  client_file = plugin_open_staging(
      config->client_source_output, &client_staging);
  if (client_file == NULL) goto cleanup;

  if (!plugin_write_header(
          header_file, canonical_ir, component,
          config, &native_ir) ||
      !plugin_write_source(
          source_file, component, config, &native_ir,
          contract_version) ||
      !plugin_write_client_source(
          client_file, canonical_ir, component,
          config, &native_ir, contract_version))
    goto cleanup;

  if (!plugin_close_staging(&header_file) ||
      !plugin_close_staging(&source_file) ||
      !plugin_close_staging(&client_file))
    goto cleanup;

  {
    const char *final_paths[] = {
        config->service_header_output,
        request->output,
        config->client_source_output
    };
    char *staging_paths[] = {
        header_staging,
        source_staging,
        client_staging
    };

    {
      int commit_ok = plugin_commit_output_set(
          final_paths, staging_paths,
          sizeof(final_paths) / sizeof(final_paths[0]));
      header_staging = staging_paths[0];
      source_staging = staging_paths[1];
      client_staging = staging_paths[2];
      if (!commit_ok) goto cleanup;
    }
  }

  result = 0;

cleanup:
  if (header_file != NULL) fclose(header_file);
  if (source_file != NULL) fclose(source_file);
  if (client_file != NULL) fclose(client_file);
  if (header_staging != NULL) {
    plugin_unlink_if_exists(header_staging);
    free(header_staging);
  }
  if (source_staging != NULL) {
    plugin_unlink_if_exists(source_staging);
    free(source_staging);
  }
  if (client_staging != NULL) {
    plugin_unlink_if_exists(client_staging);
    free(client_staging);
  }
  databind_compiler_service_native_destroy(&native_ir);
  return result;
}

const databind_compiler_projection_backend
    DATABIND_COMPILER_PLUGIN_BACKEND = {
        DATABIND_COMPILER_PROJECTION_PLUGIN,
        "plugin",
        databind_compiler_plugin_generate,
        NULL,
};

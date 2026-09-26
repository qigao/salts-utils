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
    const Node *component,
    const databind_compiler_service_native_ir *ir,
    size_t *out_selected_count) {
  const char *plugin_id =
      plugin_string(component, "qualified_name");
  size_t selected_count = 0u;
  size_t i;

  if (out_selected_count != NULL) *out_selected_count = 0u;

  if (!plugin_bounded_text_valid(plugin_id, SALTS_PLUGIN_ID_MAX) ||
      ir == NULL || ir->operations == NULL)
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
    const char *suffix,
    char *out, size_t out_size) {
  const char *parts[] = {schema_name, component_name};
  size_t used = 0u;
  size_t part;

  if (!plugin_text_valid(schema_name) ||
      !plugin_text_valid(component_name) ||
      !plugin_text_valid(suffix) ||
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

  {
    size_t suffix_length = strlen(suffix);
    if (used > SIZE_MAX - suffix_length - 1u ||
        used + suffix_length + 1u > out_size)
      return 0;
    memcpy(out + used, suffix, suffix_length + 1u);
  }
  return 1;
}

static int plugin_client_symbol(
    const Node *root,
    const Node *component,
    char *out,
    size_t out_size) {
  const char *schema_name = plugin_schema_name(root);
  const char *component_name = plugin_string(component, "name");
  char guard_probe[320];
  int written;

  if (!plugin_header_guard(
          schema_name, component_name,
          "_PLUGIN_CLIENT_H",
          guard_probe, sizeof(guard_probe)))
    return 0;

  written = snprintf(
      out, out_size,
      "databind_plugin_client_%zu_%s_%zu_%s",
      strlen(schema_name), schema_name,
      strlen(component_name), component_name);
  return written > 0 && (size_t)written < out_size;
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
    plugin_output_transaction *outputs,
    size_t output_count) {
  size_t i;
  int ok = 0;

  if (outputs == NULL || output_count == 0u) return 0;

  for (i = 0u; i < output_count; ++i)
    if (outputs[i].final_path == NULL ||
        outputs[i].staging_path == NULL)
      goto rollback;

  for (i = 0u; i < output_count; ++i)
    if (!plugin_prepare_output_backup(&outputs[i]))
      goto rollback;

  for (i = 0u; i < output_count; ++i) {
    if (salts_fs_rename(
            outputs[i].staging_path,
            outputs[i].final_path) != 0)
      goto rollback;
    outputs[i].published = 1;
  }

  for (i = 0u; i < output_count; ++i)
    if (outputs[i].had_original)
      plugin_unlink_if_exists(outputs[i].backup_path);

  ok = 1;
  goto cleanup;

rollback:
  for (i = output_count; i > 0u; --i)
    plugin_rollback_output(&outputs[i - 1u]);

cleanup:
  for (i = 0u; i < output_count; ++i)
    plugin_free_output_paths(&outputs[i]);
  return ok;
}

static int plugin_write_header(
    FILE *file,
    const Node *root,
    const Node *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir) {
  char guard[320];
  size_t i;

  if (!plugin_header_guard(
          plugin_schema_name(root),
          plugin_string(component, "name"),
          "_PLUGIN_SERVICE_H",
          guard, sizeof(guard)))
    return 0;

  if (fprintf(file, "#ifndef %s\n#define %s\n\n", guard, guard) < 0 ||
      fputs("#include \"data_bind.h\"\n#include <salts_cmeta_data.h>\n#include <string.h>\n#include ", file) == EOF ||
      !plugin_write_c_string(file, config->native_header) ||
      fputs(
          "\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i)
    if (databind_compiler_service_native_emit_prototype(
            file, &ir->operations[i]) != 0)
      return 0;

  return fprintf(
             file,
             "\n#ifdef __cplusplus\n}\n#endif\n\n"
             "#endif /* %s */\n",
             guard) >= 0;
}

static int plugin_write_client_header(
    FILE *file,
    const Node *root,
    const Node *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir) {
  const char *service_header =
      plugin_basename(config->service_header_output);
  char guard[320];
  char init_macro[320];
  char client_symbol[512];
  size_t i;

  if (!plugin_header_guard(
          plugin_schema_name(root),
          plugin_string(component, "name"),
          "_PLUGIN_CLIENT_H",
          guard, sizeof(guard)) ||
      !plugin_header_guard(
          plugin_schema_name(root),
          plugin_string(component, "name"),
          "_PLUGIN_CLIENT_INIT",
          init_macro, sizeof(init_macro)) ||
      !plugin_client_symbol(
          root, component,
          client_symbol, sizeof(client_symbol)))
    return 0;

  if (fprintf(file, "#ifndef %s\n#define %s\n\n", guard, guard) < 0 ||
      fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, service_header) ||
      fputs(
          "\n#include <salts/plugin.h>\n"
          "#include <stdbool.h>\n\n"
          "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n",
          file) == EOF ||
      fprintf(
          file,
          "typedef struct %s {\n"
          "  salts_plugin_registry *registry;\n"
          "  salts_plugin_lease lease;\n",
          client_symbol) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i)
    if (fprintf(
            file,
            "  const salts_plugin_export *%s_export;\n",
            ir->operations[i].symbol) < 0)
      return 0;

  if (fprintf(
          file,
          "} %s;\n"
          "#define %s {0}\n\n"
          "bool %s_valid(const %s *client);\n"
          "salts_plugin_status %s_open(\n"
          "    salts_plugin_registry *registry, salts_plugin_ref ref,\n"
          "    %s *out_client);\n"
          "salts_plugin_status %s_close(%s *client);\n\n",
          client_symbol, init_macro,
          client_symbol, client_symbol,
          client_symbol, client_symbol,
          client_symbol, client_symbol) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (operation->error_count == 0u) {
      if (fprintf(
              file,
              "salts_plugin_status %s_plugin_client_call(\n"
              "    %s *client,\n"
              "    const %s_t *request, %s_t *response,\n"
              "    int *native_status);\n\n",
              operation->symbol,
              client_symbol,
              operation->request_type,
              operation->response_type) < 0)
        return 0;
    } else {
      if (fprintf(
              file,
              "salts_plugin_status %s_plugin_client_call(\n"
              "    %s *client,\n"
              "    const %s_t *request, %s_t *response,\n"
              "    %s__error *typed_error, int *native_status);\n\n",
              operation->symbol,
              client_symbol,
              operation->request_type,
              operation->response_type,
              operation->symbol) < 0)
        return 0;
    }
  }

  return fprintf(
             file,
             "#ifdef __cplusplus\n}\n#endif\n\n"
             "#endif /* %s */\n",
             guard) >= 0;
}

static int plugin_write_client_source(
    FILE *file,
    const Node *root,
    const Node *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir,
    uint32_t contract_version) {
  const char *client_header =
      plugin_basename(config->client_header_output);
  const char *plugin_id =
      plugin_string(component, "qualified_name");
  char client_symbol[512];
  size_t i;

  if (!plugin_text_valid(client_header) ||
      !plugin_text_valid(plugin_id) ||
      !plugin_client_symbol(
          root, component,
          client_symbol, sizeof(client_symbol)))
    return 0;

  if (fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, client_header) ||
      fputs(
          "\n#include <cmeta/function.h>\n"
          "#include <string.h>\n\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, &ir->operations[i], 0) != 0 ||
        fputc('\n', file) == EOF)
      return 0;
  }

  if (fprintf(
          file,
          "bool %s_valid(const %s *client) {\n"
          "  return client != NULL && client->registry != NULL &&\n"
          "         salts_plugin_lease_valid(client->lease)",
          client_symbol, client_symbol) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i)
    if (fprintf(
            file,
            " &&\n         client->%s_export != NULL",
            ir->operations[i].symbol) < 0)
      return 0;

  if (fputs(";\n}\n\n", file) == EOF)
    return 0;

  if (fprintf(
          file,
          "salts_plugin_status %s_open(\n"
          "    salts_plugin_registry *registry, salts_plugin_ref ref,\n"
          "    %s *out_client) {\n"
          "  salts_plugin_status status;\n"
          "  salts_plugin_status release_status;\n"
          "  salts_plugin_lease lease = {0};\n"
          "  const salts_plugin_manifest *manifest = NULL;\n"
          "  const salts_plugin_export *entry = NULL;\n\n"
          "  if (out_client == NULL)\n"
          "    return SALTS_PLUGIN_INVALID_ARGUMENT;\n"
          "  if (out_client->registry != NULL ||\n"
          "      out_client->lease.plugin.slot != 0u ||\n"
          "      out_client->lease.plugin.generation != 0u ||\n"
          "      out_client->lease.slot != 0u ||\n"
          "      out_client->lease.generation != 0u",
          client_symbol, client_symbol) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i)
    if (fprintf(
            file,
            " ||\n      out_client->%s_export != NULL",
            ir->operations[i].symbol) < 0)
      return 0;

  if (fputs(
          ")\n"
          "    return SALTS_PLUGIN_ALREADY;\n"
          "  memset(out_client, 0, sizeof(*out_client));\n"
          "  if (registry == NULL || !salts_plugin_ref_valid(ref))\n"
          "    return SALTS_PLUGIN_INVALID_ARGUMENT;\n\n"
          "  status = salts_plugin_registry_acquire(\n"
          "      registry, ref, &lease, &manifest);\n"
          "  if (status != SALTS_PLUGIN_OK) return status;\n\n"
          "  if (manifest == NULL || manifest->plugin_id == NULL ||\n"
          "      strcmp(manifest->plugin_id, ",
          file) == EOF ||
      !plugin_write_c_string(file, plugin_id) ||
      fputs(
          ") != 0) {\n"
          "    status = SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
          "    goto fail;\n"
          "  }\n\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (fputs(
            "  entry = NULL;\n"
            "  status = salts_plugin_manifest_find_export(\n"
            "      manifest, ",
            file) == EOF ||
        !plugin_write_c_string(file, operation->qualified_operation) ||
        fputs(", &entry);\n"
              "  if (status != SALTS_PLUGIN_OK) goto fail;\n"
              "  status = salts_plugin_export_require_function(\n"
              "      entry, ",
              file) == EOF ||
        !plugin_write_c_string(file, operation->qualified_service) ||
        fprintf(
            file,
            ", %uu, 0u);\n"
            "  if (status != SALTS_PLUGIN_OK) goto fail;\n"
            "  if (!cmeta_function_desc_equal(\n"
            "          entry->value.function.desc,\n"
            "          &%s__function_meta) ||\n"
            "      !cmeta_function_abi_desc_equal(\n"
            "          entry->value.function.abi,\n"
            "          &%s__function_abi_meta)) {\n"
            "    status = SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
            "    goto fail;\n"
            "  }\n"
            "  out_client->%s_export = entry;\n\n",
            contract_version,
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return 0;
  }

  if (fprintf(
          file,
          "  out_client->registry = registry;\n"
          "  out_client->lease = lease;\n"
          "  return SALTS_PLUGIN_OK;\n\n"
          "fail:\n"
          "  release_status = salts_plugin_registry_release(\n"
          "      registry, &lease);\n"
          "  memset(out_client, 0, sizeof(*out_client));\n"
          "  if (release_status != SALTS_PLUGIN_OK) {\n"
          "    out_client->registry = registry;\n"
          "    out_client->lease = lease;\n"
          "    return release_status;\n"
          "  }\n"
          "  return status;\n"
          "}\n\n"
          "salts_plugin_status %s_close(%s *client) {\n"
          "  salts_plugin_registry *registry;\n"
          "  salts_plugin_lease lease;\n"
          "  salts_plugin_status status;\n"
          "  if (client == NULL || client->registry == NULL ||\n"
          "      !salts_plugin_lease_valid(client->lease))\n"
          "    return SALTS_PLUGIN_INVALID_ARGUMENT;\n"
          "  registry = client->registry;\n"
          "  lease = client->lease;\n"
          "  status = salts_plugin_registry_release(registry, &lease);\n"
          "  if (status == SALTS_PLUGIN_OK)\n"
          "    memset(client, 0, sizeof(*client));\n"
          "  return status;\n"
          "}\n\n",
          client_symbol, client_symbol) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (operation->error_count == 0u) {
      if (fprintf(
              file,
              "salts_plugin_status %s_plugin_client_call(\n"
              "    %s *client,\n"
              "    const %s_t *request, %s_t *response,\n"
              "    int *native_status) {\n"
              "  const salts_plugin_export *entry;\n"
              "  void *params[2];\n"
              "  int result;\n"
              "  if (client == NULL || request == NULL || response == NULL ||\n"
              "      native_status == NULL)\n"
              "    return SALTS_PLUGIN_INVALID_ARGUMENT;\n"
              "  if (client->registry == NULL ||\n"
              "      !salts_plugin_lease_valid(client->lease))\n"
              "    return SALTS_PLUGIN_INVALID_STATE;\n"
              "  entry = client->%s_export;\n"
              "  if (entry == NULL || entry->kind != SALTS_PLUGIN_EXPORT_FUNCTION ||\n"
              "      entry->value.function.invoke == NULL)\n"
              "    return SALTS_PLUGIN_INVALID_STATE;\n"
              "  params[0] = (void *)request;\n"
              "  params[1] = response;\n"
              "  if (!entry->value.function.invoke(\n"
              "          entry->value.function.context, &result,\n"
              "          params, 2u))\n"
              "    return SALTS_PLUGIN_INVALID_STATE;\n"
              "  *native_status = result;\n"
              "  return SALTS_PLUGIN_OK;\n"
              "}\n\n",
              operation->symbol,
              client_symbol,
              operation->request_type,
              operation->response_type,
              operation->symbol) < 0)
        return 0;
    } else {
      if (fprintf(
              file,
              "salts_plugin_status %s_plugin_client_call(\n"
              "    %s *client,\n"
              "    const %s_t *request, %s_t *response,\n"
              "    %s__error *typed_error, int *native_status) {\n"
              "  const salts_plugin_export *entry;\n"
              "  void *params[3];\n"
              "  %s__error local_error = %s__ERROR_INIT;\n"
              "  int result;\n"
              "  if (client == NULL || request == NULL || response == NULL ||\n"
              "      typed_error == NULL || native_status == NULL)\n"
              "    return SALTS_PLUGIN_INVALID_ARGUMENT;\n"
              "  if (client->registry == NULL ||\n"
              "      !salts_plugin_lease_valid(client->lease))\n"
              "    return SALTS_PLUGIN_INVALID_STATE;\n"
              "  entry = client->%s_export;\n"
              "  if (entry == NULL || entry->kind != SALTS_PLUGIN_EXPORT_FUNCTION ||\n"
              "      entry->value.function.invoke == NULL)\n"
              "    return SALTS_PLUGIN_INVALID_STATE;\n"
              "  params[0] = (void *)request;\n"
              "  params[1] = response;\n"
              "  %s__error_init(&local_error);\n"
              "  params[2] = &local_error;\n"
              "  if (!entry->value.function.invoke(\n"
              "          entry->value.function.context, &result,\n"
              "          params, 3u)) {\n"
              "    (void)%s__error_clear(&local_error);\n"
              "    return SALTS_PLUGIN_INVALID_STATE;\n"
              "  }\n"
              "  if (%s__error_move(typed_error, &local_error) != DATA_BIND_OK) {\n"
              "    (void)%s__error_clear(&local_error);\n"
              "    return SALTS_PLUGIN_INVALID_STATE;\n"
              "  }\n"
              "  *native_status = result;\n"
              "  return SALTS_PLUGIN_OK;\n"
              "}\n\n",
              operation->symbol,
              client_symbol,
              operation->request_type,
              operation->response_type,
              operation->symbol,
              operation->symbol,
              operation->symbol,
              operation->symbol,
              operation->symbol,
              operation->symbol,
              operation->symbol,
              operation->symbol) < 0)
        return 0;
    }
  }

  return 1;
}

static int plugin_write_adapter(
    FILE *file,
    const databind_compiler_service_native_operation *operation) {
  if (file == NULL || operation == NULL ||
      !plugin_text_valid(operation->symbol) ||
      !plugin_text_valid(operation->request_type) ||
      !plugin_text_valid(operation->response_type))
    return 0;

  if (operation->error_count == 0u) {
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

  return fprintf(
             file,
             "static bool SALTS_PLUGIN_CALL %s__plugin_invoke(\n"
             "    void *context, void *return_storage, void *const *params,\n"
             "    size_t param_count) {\n"
             "  int result;\n"
             "  (void)context;\n"
             "  if (return_storage == NULL || params == NULL ||\n"
             "      param_count != 3u || params[0] == NULL ||\n"
             "      params[1] == NULL || params[2] == NULL)\n"
             "    return false;\n"
             "  result = %s((const %s_t *)params[0], (%s_t *)params[1],\n"
             "              (%s__error *)params[2]);\n"
             "  *(int *)return_storage = result;\n"
             "  return true;\n"
             "}\n\n",
             operation->symbol,
             operation->symbol,
             operation->request_type,
             operation->response_type,
             operation->symbol) >= 0;
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
  char *client_header_staging = NULL;
  char *client_source_staging = NULL;
  FILE *header_file = NULL;
  FILE *source_file = NULL;
  FILE *client_header_file = NULL;
  FILE *client_source_file = NULL;
  plugin_output_transaction outputs[4] = {{0}};
  int result = -1;
  (void)context;

  if (canonical_ir == NULL || request == NULL ||
      request->id.axis != DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT ||
      request->id.kind != DATABIND_COMPILER_ARTIFACT_PLUGIN ||
      config == NULL ||
      !plugin_text_valid(request->output) ||
      !plugin_text_valid(config->component_id) ||
      !plugin_text_valid(config->native_header) ||
      !plugin_text_valid(config->service_header_output) ||
      !plugin_text_valid(config->client_header_output) ||
      !plugin_text_valid(config->client_source_output) ||
      strcmp(request->output, config->service_header_output) == 0 ||
      strcmp(request->output, config->client_header_output) == 0 ||
      strcmp(request->output, config->client_source_output) == 0 ||
      strcmp(config->service_header_output,
             config->client_header_output) == 0 ||
      strcmp(config->service_header_output,
             config->client_source_output) == 0 ||
      strcmp(config->client_header_output,
             config->client_source_output) == 0 ||
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
          component, &native_ir,
          &selected_count) ||
      selected_count != native_ir.operation_count)
    goto cleanup;

  header_file = plugin_open_staging(
      config->service_header_output, &header_staging);
  if (header_file == NULL) goto cleanup;

  source_file = plugin_open_staging(
      request->output, &source_staging);
  if (source_file == NULL) goto cleanup;

  client_header_file = plugin_open_staging(
      config->client_header_output, &client_header_staging);
  if (client_header_file == NULL) goto cleanup;

  client_source_file = plugin_open_staging(
      config->client_source_output, &client_source_staging);
  if (client_source_file == NULL) goto cleanup;

  if (!plugin_write_header(
          header_file, canonical_ir, component,
          config, &native_ir) ||
      !plugin_write_source(
          source_file, component, config, &native_ir,
          contract_version) ||
      !plugin_write_client_header(
          client_header_file, canonical_ir, component,
          config, &native_ir) ||
      !plugin_write_client_source(
          client_source_file, canonical_ir, component,
          config, &native_ir, contract_version))
    goto cleanup;

  if (!plugin_close_staging(&header_file) ||
      !plugin_close_staging(&source_file) ||
      !plugin_close_staging(&client_header_file) ||
      !plugin_close_staging(&client_source_file))
    goto cleanup;

  outputs[0].final_path = config->service_header_output;
  outputs[0].staging_path = header_staging;
  outputs[1].final_path = request->output;
  outputs[1].staging_path = source_staging;
  outputs[2].final_path = config->client_header_output;
  outputs[2].staging_path = client_header_staging;
  outputs[3].final_path = config->client_source_output;
  outputs[3].staging_path = client_source_staging;

  header_staging = NULL;
  source_staging = NULL;
  client_header_staging = NULL;
  client_source_staging = NULL;

  if (!plugin_commit_output_set(
          outputs, sizeof(outputs) / sizeof(outputs[0])))
    goto cleanup;

  result = 0;

cleanup:
  if (header_file != NULL) fclose(header_file);
  if (source_file != NULL) fclose(source_file);
  if (client_header_file != NULL) fclose(client_header_file);
  if (client_source_file != NULL) fclose(client_source_file);

  if (header_staging != NULL) {
    plugin_unlink_if_exists(header_staging);
    free(header_staging);
  }
  if (source_staging != NULL) {
    plugin_unlink_if_exists(source_staging);
    free(source_staging);
  }
  if (client_header_staging != NULL) {
    plugin_unlink_if_exists(client_header_staging);
    free(client_header_staging);
  }
  if (client_source_staging != NULL) {
    plugin_unlink_if_exists(client_source_staging);
    free(client_source_staging);
  }

  databind_compiler_service_native_destroy(&native_ir);
  return result;
}

const databind_compiler_projection_backend
    DATABIND_COMPILER_PLUGIN_BACKEND = {
        {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
         DATABIND_COMPILER_ARTIFACT_PLUGIN},
        "plugin",
        databind_compiler_plugin_generate,
        NULL,
};

#include "plugin_projection.h"

#include "service_native.h"
#include "native_service_projection.h"
#include "cmeta_fs.h"

#include <salts/plugin.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *plugin_schema_name(const IdlContract *contract) {
  return contract != NULL ? contract->name : NULL;
}

static int plugin_schema_contract_version(
    const IdlContract *contract, uint32_t *out_version) {
  const char *text = contract != NULL ? contract->version : NULL;
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

static int plugin_component_has_service(
    const IdlComponent *component, const char *service_name) {
  size_t i;

  if (component == NULL || service_name == NULL) return 0;
  for (i = 0u; i < component->capability_count; ++i) {
    const IdlComponentCapability *capability =
        &component->capabilities[i];
    if (capability->kind == IDL_CAPABILITY_SERVICE &&
        capability->name != NULL &&
        strcmp(capability->name, service_name) == 0)
      return 1;
  }
  return 0;
}

static int plugin_operation_selected(
    const IdlComponent *component,
    const databind_compiler_service_native_operation *operation) {
  return operation != NULL &&
         plugin_component_has_service(
             component, operation->service_name);
}

static int plugin_select_component_service(
    void *context, const char *service_name) {
  return plugin_component_has_service(
      (const IdlComponent *)context, service_name);
}

static int plugin_native_ir_valid(
    const IdlComponent *component,
    const databind_compiler_service_native_ir *ir,
    size_t *out_selected_count) {
  const char *plugin_id =
      component != NULL ? component->qualified_name : NULL;
  size_t selected_count = 0u;
  size_t i;

  if (out_selected_count != NULL) *out_selected_count = 0u;

  if (!plugin_bounded_text_valid(plugin_id, CMETA_PLUGIN_ID_MAX) ||
      ir == NULL || ir->operations == NULL)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (!plugin_operation_selected(component, operation))
      continue;

    if (!plugin_bounded_text_valid(
            operation->qualified_service,
            CMETA_PLUGIN_CONTRACT_ID_MAX) ||
        !plugin_bounded_text_valid(
            operation->qualified_operation,
            CMETA_PLUGIN_EXPORT_ID_MAX) ||
        !plugin_text_valid(operation->symbol) ||
        !plugin_text_valid(operation->request_type) ||
        !plugin_text_valid(operation->response_type))
      return 0;

    ++selected_count;
    if (selected_count >= CMETA_PLUGIN_MAX_EXPORTS)
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
    const IdlContract *contract,
    const IdlComponent *component,
    char *out,
    size_t out_size) {
  const char *schema_name = plugin_schema_name(contract);
  const char *component_name =
      component != NULL ? component->name : NULL;
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
      cmeta_fs_access(path, SALTS_FS_ACCESS_EXISTS) == 0)
    (void)cmeta_fs_unlink(path);
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

  if (cmeta_fs_access(
          output->final_path, SALTS_FS_ACCESS_EXISTS) == 0) {
    if (cmeta_fs_rename(
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
    (void)cmeta_fs_rename(
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
    if (cmeta_fs_rename(
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
    const IdlContract *contract,
    const IdlComponent *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir) {
  char guard[320];
  size_t i;

  if (!plugin_header_guard(
          plugin_schema_name(contract),
          component != NULL ? component->name : NULL,
          "_PLUGIN_SERVICE_H",
          guard, sizeof(guard)))
    return 0;

  if (fprintf(file, "#ifndef %s\n#define %s\n\n", guard, guard) < 0 ||
      fputs("#include \"data_bind.h\"\n#include <cmeta_cmeta_data.h>\n#include <string.h>\n#include ", file) == EOF ||
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
    const IdlContract *contract,
    const IdlComponent *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir) {
  const char *service_header =
      plugin_basename(config->service_header_output);
  char guard[320];
  char init_macro[320];
  char client_symbol[512];
  size_t i;

  if (!plugin_header_guard(
          plugin_schema_name(contract),
          component != NULL ? component->name : NULL,
          "_PLUGIN_CLIENT_H",
          guard, sizeof(guard)) ||
      !plugin_header_guard(
          plugin_schema_name(contract),
          component != NULL ? component->name : NULL,
          "_PLUGIN_CLIENT_INIT",
          init_macro, sizeof(init_macro)) ||
      !plugin_client_symbol(
          contract, component,
          client_symbol, sizeof(client_symbol)))
    return 0;

  if (fprintf(file, "#ifndef %s\n#define %s\n\n", guard, guard) < 0 ||
      fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, service_header) ||
      fputs(
          "\n#include <data_bind_plugin_catalog.h>\n"
          "#include <salts/plugin.h>\n"
          "#include <stdbool.h>\n\n"
          "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n",
          file) == EOF ||
      fprintf(
          file,
          "/* Owning generated Plugin client. One live client owns one Plugin lease.\n"
          " * Cached operation snapshots, *_export pointers, and their reachable\n"
          " * FunctionDesc/FunctionAbi/DataDesc/GenericDesc/callable views are\n"
          " * borrowed from that lease. They expire when close succeeds; callers\n"
          " * must not retain or dereference those views after close. */\n"
          "typedef struct %s {\n"
          "  cmeta_plugin_registry *registry;\n"
          "  cmeta_plugin_lease lease;\n",
          client_symbol) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i)
    if (fprintf(
            file,
            "  DataBindPluginOperationBinding %s_operation;\n"
            "  const cmeta_plugin_export *%s_export;\n",
            ir->operations[i].symbol,
            ir->operations[i].symbol) < 0)
      return 0;

  if (fprintf(
          file,
          "} %s;\n"
          "#define %s {0}\n\n"
          "bool %s_valid(const %s *client);\n"
          "cmeta_plugin_status %s_open(\n"
          "    cmeta_plugin_registry *registry, cmeta_plugin_ref ref,\n"
          "    %s *out_client);\n"
          "cmeta_plugin_status %s_close(%s *client);\n\n",
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
              "cmeta_plugin_status %s_plugin_client_call(\n"
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
              "cmeta_plugin_status %s_plugin_client_call(\n"
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
    const IdlContract *contract,
    const IdlComponent *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir,
    uint32_t contract_version) {
  const char *client_header =
      plugin_basename(config->client_header_output);
  const char *plugin_id =
      component != NULL ? component->qualified_name : NULL;
  char client_symbol[512];
  size_t i;

  if (!plugin_text_valid(client_header) ||
      !plugin_text_valid(plugin_id) ||
      !plugin_client_symbol(
          contract, component,
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
          "static DataBindStatus %s_catalog_operation_find(\n"
          "    data_bind_plugin_catalog *catalog, const char *export_id,\n"
          "    DataBindPluginOperationBinding *out) {\n"
          "  DataBindError error = DATA_BIND_ERROR_INIT;\n"
          "  size_t count;\n"
          "  size_t index;\n"
          "  if (catalog == NULL || export_id == NULL || out == NULL ||\n"
          "      !data_bind_plugin_catalog_valid(catalog))\n"
          "    return DATA_BIND_ERR_INVALID_ARG;\n"
          "  *out = (DataBindPluginOperationBinding)\n"
          "      DATA_BIND_PLUGIN_OPERATION_BINDING_INIT;\n"
          "  count = data_bind_plugin_catalog_operation_count(catalog);\n"
          "  for (index = 0u; index < count; ++index) {\n"
          "    DataBindPluginOperationBinding candidate =\n"
          "        DATA_BIND_PLUGIN_OPERATION_BINDING_INIT;\n"
          "    DataBindStatus status = data_bind_plugin_catalog_operation_at(\n"
          "        catalog, index, &candidate, &error);\n"
          "    if (status != DATA_BIND_OK) return status;\n"
          "    if (candidate.export_id != NULL &&\n"
          "        strcmp(candidate.export_id, export_id) == 0) {\n"
          "      if (!data_bind_plugin_operation_binding_valid(&candidate))\n"
          "        return DATA_BIND_ERR_RUNTIME;\n"
          "      *out = candidate;\n"
          "      return DATA_BIND_OK;\n"
          "    }\n"
          "  }\n"
          "  return DATA_BIND_ERR_TYPE_NOT_FOUND;\n"
          "}\n\n",
          client_symbol) < 0)
    return 0;

  if (fprintf(
          file,
          "bool %s_valid(const %s *client) {\n"
          "  return client != NULL && client->registry != NULL &&\n"
          "         cmeta_plugin_lease_valid(client->lease)",
          client_symbol, client_symbol) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i)
    if (fprintf(
            file,
            " &&\n         data_bind_plugin_operation_binding_valid("
            "&client->%s_operation) &&"
            "\n         client->%s_export != NULL",
            ir->operations[i].symbol,
            ir->operations[i].symbol) < 0)
      return 0;

  if (fputs(";\n}\n\n", file) == EOF)
    return 0;

  /* Generated Plugin lease cleanup is CLEAR_ON_SUCCESS: a failed registry
   * release leaves the owning handle intact so the caller can retry. */
  if (fprintf(
          file,
          "static cmeta_plugin_status %s__release_owned_lease(%s *client) {\n"
          "  cmeta_plugin_registry *registry;\n"
          "  cmeta_plugin_lease lease;\n"
          "  cmeta_plugin_status status;\n"
          "  if (client == NULL || client->registry == NULL ||\n"
          "      !cmeta_plugin_lease_valid(client->lease))\n"
          "    return CMETA_PLUGIN_INVALID_ARGUMENT;\n"
          "  registry = client->registry;\n"
          "  lease = client->lease;\n"
          "  status = cmeta_plugin_registry_release(registry, &lease);\n"
          "  if (status == CMETA_PLUGIN_OK)\n"
          "    memset(client, 0, sizeof(*client));\n"
          "  return status;\n"
          "}\n\n",
          client_symbol, client_symbol) < 0)
    return 0;

  if (fprintf(
          file,
          "cmeta_plugin_status %s_open(\n"
          "    cmeta_plugin_registry *registry, cmeta_plugin_ref ref,\n"
          "    %s *out_client) {\n"
          "  cmeta_plugin_status status;\n"
          "  cmeta_plugin_status release_status;\n"
          "  cmeta_plugin_lease lease = {0};\n"
          "  const cmeta_plugin_manifest *manifest = NULL;\n"
          "  const cmeta_plugin_export *catalog_entry = NULL;\n"
          "  const cmeta_plugin_export *entry = NULL;\n"
          "  data_bind_plugin_catalog *catalog = NULL;\n\n"
          "  if (out_client == NULL)\n"
          "    return CMETA_PLUGIN_INVALID_ARGUMENT;\n"
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
            " ||\n      out_client->%s_operation.size != 0u"
            " ||\n      out_client->%s_export != NULL",
            ir->operations[i].symbol,
            ir->operations[i].symbol) < 0)
      return 0;

  if (fputs(
          ")\n"
          "    return CMETA_PLUGIN_ALREADY;\n"
          "  memset(out_client, 0, sizeof(*out_client));\n"
          "  if (registry == NULL || !cmeta_plugin_ref_valid(ref))\n"
          "    return CMETA_PLUGIN_INVALID_ARGUMENT;\n\n"
          "  status = cmeta_plugin_registry_acquire(\n"
          "      registry, ref, &lease, &manifest);\n"
          "  if (status != CMETA_PLUGIN_OK) return status;\n\n"
          "  if (manifest == NULL || manifest->plugin_id == NULL ||\n"
          "      strcmp(manifest->plugin_id, ",
          file) == EOF ||
      !plugin_write_c_string(file, plugin_id) ||
      fputs(
          ") != 0) {\n"
          "    status = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
          "    goto fail;\n"
          "  }\n\n",
          file) == EOF)
    return 0;

  /* First preserve exact FUNCTION export admission and its error ordering. */
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (fputs(
            "  entry = NULL;\n"
            "  status = cmeta_plugin_manifest_find_export(\n"
            "      manifest, ",
            file) == EOF ||
        !plugin_write_c_string(file, operation->qualified_operation) ||
        fputs(", &entry);\n"
              "  if (status != CMETA_PLUGIN_OK) goto fail;\n"
              "  status = cmeta_plugin_export_require_function(\n"
              "      entry, ",
              file) == EOF ||
        !plugin_write_c_string(file, operation->qualified_service) ||
        fprintf(
            file,
            ", %uu, 0u);\n"
            "  if (status != CMETA_PLUGIN_OK) goto fail;\n"
            "  if (!cmeta_function_desc_equal(\n"
            "          entry->value.function.desc,\n"
            "          &%s__function_meta) ||\n"
            "      !cmeta_function_abi_desc_equal(\n"
            "          entry->value.function.abi,\n"
            "          &%s__function_abi_meta)) {\n"
            "    status = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
            "    goto fail;\n"
            "  }\n"
            "  out_client->%s_export = entry;\n\n",
            contract_version,
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return 0;
  }

  /* The same client lease also owns the canonical DataBind catalog graph. */
  if (fputs(
          "  status = cmeta_plugin_manifest_find_export(\n"
          "      manifest, DATA_BIND_PLUGIN_CATALOG_EXPORT_ID,\n"
          "      &catalog_entry);\n"
          "  if (status != CMETA_PLUGIN_OK) goto fail;\n"
          "  status = cmeta_plugin_export_require_interface(\n"
          "      catalog_entry, DATA_BIND_PLUGIN_CATALOG_CONTRACT_ID,\n"
          "      DATA_BIND_PLUGIN_CATALOG_CONTRACT_VERSION, 0u,\n"
          "      data_bind_plugin_catalog_interface());\n"
          "  if (status != CMETA_PLUGIN_OK) goto fail;\n"
          "  catalog = (data_bind_plugin_catalog *)\n"
          "      catalog_entry->value.interface.value;\n"
          "  if (!data_bind_plugin_catalog_valid(catalog)) {\n"
          "    status = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
          "    goto fail;\n"
          "  }\n\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (fprintf(
            file,
            "  if (%s_catalog_operation_find(catalog, ",
            client_symbol) < 0 ||
        !plugin_write_c_string(file, operation->qualified_operation) ||
        fprintf(
            file,
            ", &out_client->%s_operation) != DATA_BIND_OK) {\n"
            "    status = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
            "    goto fail;\n"
            "  }\n"
            "  if (!cmeta_function_desc_equal(\n"
            "          out_client->%s_operation.function,\n"
            "          &%s__function_meta) ||\n"
            "      !cmeta_function_desc_equal(\n"
            "          out_client->%s_operation.function,\n"
            "          out_client->%s_export->value.function.desc)) {\n"
            "    status = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
            "    goto fail;\n"
            "  }\n\n",
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return 0;
  }

  if (fprintf(
          file,
          "  out_client->registry = registry;\n"
          "  out_client->lease = lease;\n"
          "  return CMETA_PLUGIN_OK;\n\n"
          "fail:\n"
          "  memset(out_client, 0, sizeof(*out_client));\n"
          "  out_client->registry = registry;\n"
          "  out_client->lease = lease;\n"
          "  release_status = %s__release_owned_lease(out_client);\n"
          "  if (release_status != CMETA_PLUGIN_OK)\n"
          "    return release_status;\n"
          "  return status;\n"
          "}\n\n"
          "cmeta_plugin_status %s_close(%s *client) {\n"
          "  return %s__release_owned_lease(client);\n"
          "}\n\n",
          client_symbol, client_symbol, client_symbol, client_symbol) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (operation->error_count == 0u) {
      if (fprintf(
              file,
              "cmeta_plugin_status %s_plugin_client_call(\n"
              "    %s *client,\n"
              "    const %s_t *request, %s_t *response,\n"
              "    int *native_status) {\n"
              "  const cmeta_plugin_export *entry;\n"
              "  void *params[2];\n"
              "  int result;\n"
              "  if (client == NULL || request == NULL || response == NULL ||\n"
              "      native_status == NULL)\n"
              "    return CMETA_PLUGIN_INVALID_ARGUMENT;\n"
              "  if (client->registry == NULL ||\n"
              "      !cmeta_plugin_lease_valid(client->lease))\n"
              "    return CMETA_PLUGIN_INVALID_STATE;\n"
              "  entry = client->%s_export;\n"
              "  if (entry == NULL || entry->kind != CMETA_PLUGIN_EXPORT_FUNCTION ||\n"
              "      entry->value.function.invoke == NULL)\n"
              "    return CMETA_PLUGIN_INVALID_STATE;\n"
              "  params[0] = (void *)request;\n"
              "  params[1] = response;\n"
              "  if (!entry->value.function.invoke(\n"
              "          entry->value.function.context, &result,\n"
              "          params, 2u))\n"
              "    return CMETA_PLUGIN_INVALID_STATE;\n"
              "  *native_status = result;\n"
              "  return CMETA_PLUGIN_OK;\n"
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
              "cmeta_plugin_status %s_plugin_client_call(\n"
              "    %s *client,\n"
              "    const %s_t *request, %s_t *response,\n"
              "    %s__error *typed_error, int *native_status) {\n"
              "  const cmeta_plugin_export *entry;\n"
              "  void *params[3];\n"
              "  %s__error local_error = %s__ERROR_INIT;\n"
              "  int result;\n"
              "  if (client == NULL || request == NULL || response == NULL ||\n"
              "      typed_error == NULL || native_status == NULL)\n"
              "    return CMETA_PLUGIN_INVALID_ARGUMENT;\n"
              "  if (client->registry == NULL ||\n"
              "      !cmeta_plugin_lease_valid(client->lease))\n"
              "    return CMETA_PLUGIN_INVALID_STATE;\n"
              "  entry = client->%s_export;\n"
              "  if (entry == NULL || entry->kind != CMETA_PLUGIN_EXPORT_FUNCTION ||\n"
              "      entry->value.function.invoke == NULL)\n"
              "    return CMETA_PLUGIN_INVALID_STATE;\n"
              "  params[0] = (void *)request;\n"
              "  params[1] = response;\n"
              "  %s__error_init(&local_error);\n"
              "  params[2] = &local_error;\n"
              "  if (!entry->value.function.invoke(\n"
              "          entry->value.function.context, &result,\n"
              "          params, 3u)) {\n"
              "    (void)%s__error_clear(&local_error);\n"
              "    return CMETA_PLUGIN_INVALID_STATE;\n"
              "  }\n"
              "  if (%s__error_move(typed_error, &local_error) != DATA_BIND_OK) {\n"
              "    (void)%s__error_clear(&local_error);\n"
              "    return CMETA_PLUGIN_INVALID_STATE;\n"
              "  }\n"
              "  *native_status = result;\n"
              "  return CMETA_PLUGIN_OK;\n"
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

static int plugin_write_source(
    FILE *file,
    const IdlComponent *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir,
    uint32_t contract_version) {
  const char *service_header =
      plugin_basename(config->service_header_output);
  const char *plugin_id =
      component != NULL ? component->qualified_name : NULL;
  size_t i;

  if (fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, service_header) ||
      fputs(
          "\n#include <salts/plugin.h>\n"
          "#include <data_bind_native_binding.h>\n"
          "#include <data_bind_plugin_catalog.h>\n"
          "#include <cmeta/function.h>\n\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, &ir->operations[i], 0) != 0 ||
        fputc('\n', file) == EOF ||
        databind_compiler_service_native_emit_execution(
            file, &ir->operations[i], 0) != 0 ||
        databind_compiler_service_native_emit_binding(
            file, &ir->operations[i]) != 0 ||
        fputc('\n', file) == EOF)
      return 0;
  }

  if (ir->operation_count == 0u ||
      ir->operations[0].schema_name == NULL)
    return 0;

  if (fprintf(
          file,
          "static DataBindStatus databind_plugin_catalog_create_codec(\n"
          "    void *self, DataBind **out_codec, DataBindError *error) {\n"
          "  (void)self;\n"
          "  return %s_codec_create(out_codec, error);\n"
          "}\n\n"
          "static size_t databind_plugin_catalog_operation_count(void *self) {\n"
          "  (void)self;\n"
          "  return %zuu;\n"
          "}\n\n"
          "static DataBindStatus databind_plugin_catalog_operation_at(\n"
          "    void *self, size_t index, DataBindPluginOperationBinding *out,\n"
          "    DataBindError *error) {\n"
          "  DataBindStatus status;\n"
          "  DataBindServiceNativeBinding native =\n"
          "      DATA_BIND_SERVICE_NATIVE_BINDING_INIT(NULL, NULL, NULL);\n"
          "  (void)self;\n"
          "  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;\n"
          "  *out = (DataBindPluginOperationBinding)\n"
          "      DATA_BIND_PLUGIN_OPERATION_BINDING_INIT;\n"
          "  switch (index) {\n",
          ir->operations[0].schema_name,
          ir->operation_count) < 0)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];
    if (fprintf(file, "  case %zuu:\n", i) < 0 ||
        fputs("    out->export_id = ", file) == EOF ||
        !plugin_write_c_string(file, operation->qualified_operation) ||
        fputs(";\n    out->service_name = ", file) == EOF ||
        !plugin_write_c_string(file, operation->service_name) ||
        fputs(";\n    out->operation_name = ", file) == EOF ||
        !plugin_write_c_string(file, operation->operation_name) ||
        fprintf(
            file,
            ";\n"
            "    native = (DataBindServiceNativeBinding)\n"
            "        DATA_BIND_SERVICE_NATIVE_BINDING_INIT(NULL, NULL, NULL);\n"
            "    status = %s__databind_native_binding(\n"
            "        &out->request, &out->response, &native, error);\n"
            "    if (status != DATA_BIND_OK) {\n"
            "      *out = (DataBindPluginOperationBinding)\n"
            "          DATA_BIND_PLUGIN_OPERATION_BINDING_INIT;\n"
            "      return status;\n"
            "    }\n"
            "    out->function = native.function;\n"
            "    out->errors = native.errors;\n"
            "    out->error_count = native.error_count;\n"
            "    out->error_param_index = native.error_param_index;\n"
            "    out->error_envelope_bytes = native.error_envelope_bytes;\n"
            "    out->error_kind_offset = native.error_kind_offset;\n"
            "    out->error_kind_bytes = native.error_kind_bytes;\n"
            "    return data_bind_plugin_operation_binding_valid(out)\n"
            "               ? DATA_BIND_OK : DATA_BIND_ERR_RUNTIME;\n",
            operation->symbol) < 0)
      return 0;
  }

  if (fputs(
          "  default:\n"
          "    return DATA_BIND_ERR_INVALID_ARG;\n"
          "  }\n"
          "}\n\n"
          "CMETA_IMPLEMENTS(data_bind_plugin_catalog,\n"
          "                 databind_generated_service_catalog, 0u,\n"
          "    .create_codec = databind_plugin_catalog_create_codec,\n"
          "    .operation_count = databind_plugin_catalog_operation_count,\n"
          "    .operation_at = databind_plugin_catalog_operation_at\n"
          ");\n"
          "static char databind_plugin_catalog_token;\n"
          "static data_bind_plugin_catalog databind_plugin_catalog_value = {\n"
          "    &databind_plugin_catalog_token,\n"
          "    &databind_generated_service_catalog_vtable\n"
          "};\n\n"
          "static const cmeta_plugin_export databind_plugin_exports[] = {\n",
          file) == EOF)
    return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *operation =
        &ir->operations[i];

    if (fputs(
            "  {\n"
            "    .struct_size = CMETA_PLUGIN_EXPORT_SIZE,\n"
            "    .kind = CMETA_PLUGIN_EXPORT_FUNCTION,\n",
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
            "      .invoke = %s__databind_invoke,\n"
            "    },\n"
            "  },\n",
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return 0;
  }

  if (fputs(
          "  {\n"
          "    .struct_size = CMETA_PLUGIN_EXPORT_SIZE,\n"
          "    .kind = CMETA_PLUGIN_EXPORT_INTERFACE,\n"
          "    .contract_version = DATA_BIND_PLUGIN_CATALOG_CONTRACT_VERSION,\n"
          "    .capabilities = 0u,\n"
          "    .export_id = DATA_BIND_PLUGIN_CATALOG_EXPORT_ID,\n"
          "    .contract_id = DATA_BIND_PLUGIN_CATALOG_CONTRACT_ID,\n"
          "    .value.interface = {\n"
          "      .desc = &data_bind_plugin_catalog_interface_meta,\n"
          "      .value = &databind_plugin_catalog_value,\n"
          "    },\n"
          "  },\n"
          "};\n\n"
          "static const cmeta_plugin_manifest databind_plugin_manifest = {\n"
          "  .struct_size = CMETA_PLUGIN_MANIFEST_SIZE,\n"
          "  .abi_version = CMETA_PLUGIN_ABI_VERSION,\n"
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
          "CMETA_PLUGIN_QUERY_EXPORT\n"
          "const cmeta_plugin_manifest *CMETA_PLUGIN_CALL\n"
          "cmeta_plugin_query(uint32_t host_abi) {\n"
          "  return host_abi == CMETA_PLUGIN_ABI_VERSION\n"
          "             ? &databind_plugin_manifest\n"
          "             : NULL;\n"
          "}\n",
          config->plugin_version_major,
          config->plugin_version_minor,
          config->plugin_version_patch,
          ir->operation_count + 1u) < 0)
    return 0;

  return 1;
}


/* Contract-only Plugin publication. The provider owns ordinary C records and
 * exports a canonical CMeta Function ABI. Lease acquisition, not a private
 * Binary codec/catalog, grants clients access to the typed exported function.
 * Every artifact is written to coordinator-owned stages before publication. */
static int plugin_contract_write_header(
    FILE *file, const IdlContract *contract, const IdlComponent *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir) {
  char guard[320];
  size_t i;
  if (!plugin_header_guard(plugin_schema_name(contract),
          component != NULL ? component->name : NULL,
          "_CONTRACT_PLUGIN_H", guard, sizeof(guard)))
    return 0;
  if (fprintf(file, "#ifndef %s\n#define %s\n"
                    "#ifndef DATABIND_NATIVE_ENABLE_CMETA\n"
                    "#define DATABIND_NATIVE_ENABLE_CMETA\n#endif\n"
                    "#ifndef DATABIND_NATIVE_ENABLE_DATABIND\n"
                    "#define DATABIND_NATIVE_ENABLE_DATABIND\n#endif\n",
              guard, guard) < 0 ||
      fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, config->native_header) ||
      fputs("\n#include <data_bind_binding_plan.h>\n"
            "#include <cmeta/function.h>\n"
            "#include <salts/plugin.h>\n"
            "#ifdef __cplusplus\nextern \"C\" {\n#endif\n", file) == EOF)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i)
    if (!databind_compiler_native_contract_emit_header_op(
            file, &ir->operations[i]))
      return 0;
  return fprintf(file, "#ifdef __cplusplus\n}\n#endif\n#endif /* %s */\n",
                 guard) >= 0;
}

static int plugin_contract_write_provider(
    FILE *file, const IdlComponent *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir,
    uint32_t contract_version) {
  const char *provider_header = plugin_basename(config->service_header_output);
  size_t i;
  if (!plugin_text_valid(provider_header) ||
      fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, provider_header) ||
      fputs("\n#include <string.h>\n#include <salts/plugin.h>\n", file) == EOF)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (!databind_compiler_native_contract_emit_reflection(file, op) ||
        !databind_compiler_native_contract_emit_invoke(file, op) ||
        !databind_compiler_native_contract_emit_binding(file, op))
      return 0;
  }
  if (fputs("static const cmeta_plugin_export databind_plugin_exports[] = {\n",
            file) == EOF)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (fprintf(file,
          "  {\n"
          "    .struct_size = CMETA_PLUGIN_EXPORT_SIZE,\n"
          "    .kind = CMETA_PLUGIN_EXPORT_FUNCTION,\n"
          "    .contract_version = %uu,\n"
          "    .capabilities = 0u,\n"
          "    .export_id = ", contract_version) < 0 ||
        !plugin_write_c_string(file, op->qualified_operation) ||
        fputs(",\n    .contract_id = ", file) == EOF ||
        !plugin_write_c_string(file, op->qualified_service) ||
        fprintf(file,
          ",\n"
          "    .value.function = {\n"
          "      .desc = &%s__function_meta,\n"
          "      .abi = &%s__function_abi_meta,\n"
          "      .context = NULL,\n"
          "      .invoke = %s__databind_invoke\n"
          "    }\n"
          "  },\n",
          op->symbol, op->symbol, op->symbol) < 0)
      return 0;
  }
  if (fputs("};\n"
            "static const cmeta_plugin_manifest databind_plugin_manifest = {\n"
            "  .struct_size = CMETA_PLUGIN_MANIFEST_SIZE,\n"
            "  .abi_version = CMETA_PLUGIN_ABI_VERSION,\n"
            "  .plugin_id = ", file) == EOF ||
      !plugin_write_c_string(file, component->qualified_name) ||
      fprintf(file,
            ",\n  .version = {%uu, %uu, %uu},\n"
            "  .exports = databind_plugin_exports,\n"
            "  .export_count = %zuu\n"
            "};\n"
            "CMETA_PLUGIN_QUERY_EXPORT\n"
            "const cmeta_plugin_manifest *CMETA_PLUGIN_CALL\n"
            "cmeta_plugin_query(uint32_t host_abi) {\n"
            "  return host_abi == CMETA_PLUGIN_ABI_VERSION\n"
            "      ? &databind_plugin_manifest : NULL;\n"
            "}\n",
            config->plugin_version_major,
            config->plugin_version_minor,
            config->plugin_version_patch, ir->operation_count) < 0)
    return 0;
  return 1;
}

static int plugin_contract_write_client_header(
    FILE *file, const IdlContract *contract, const IdlComponent *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir) {
  char guard[320], client_symbol[512];
  const char *provider_header = plugin_basename(config->service_header_output);
  size_t i;
  if (!plugin_header_guard(plugin_schema_name(contract), component->name,
          "_CONTRACT_PLUGIN_CLIENT_H", guard, sizeof(guard)) ||
      !plugin_client_symbol(contract, component,
          client_symbol, sizeof(client_symbol)) ||
      !plugin_text_valid(provider_header))
    return 0;
  if (fprintf(file, "#ifndef %s\n#define %s\n#include ", guard, guard) < 0 ||
      !plugin_write_c_string(file, provider_header) ||
      fprintf(file,
          "\n#include <salts/plugin.h>\n#include <stdbool.h>\n"
          "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
          "typedef struct %s {\n"
          "  cmeta_plugin_registry *registry;\n"
          "  cmeta_plugin_lease lease;\n",
          client_symbol) < 0)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i)
    if (fprintf(file, "  const cmeta_plugin_export *%s_export;\n",
                ir->operations[i].symbol) < 0)
      return 0;
  if (fprintf(file,
        "} %s;\n"
        "bool %s_valid(const %s *client);\n"
        "cmeta_plugin_status %s_open(\n"
        "    cmeta_plugin_registry *registry, cmeta_plugin_ref ref,\n"
        "    %s *out_client);\n"
        "cmeta_plugin_status %s_close(%s *client);\n",
        client_symbol,
        client_symbol, client_symbol,
        client_symbol, client_symbol,
        client_symbol, client_symbol) < 0)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (fprintf(file,
          "cmeta_plugin_status %s_plugin_client_call(\n"
          "    %s *client, const %s *request, %s *response,\n"
          "    int *native_status);\n",
          op->symbol, client_symbol,
          op->request_type, op->response_type) < 0)
      return 0;
  }
  return fprintf(file, "#ifdef __cplusplus\n}\n#endif\n#endif /* %s */\n",
                 guard) >= 0;
}

static int plugin_contract_write_client_source(
    FILE *file, const IdlContract *contract, const IdlComponent *component,
    const databind_compiler_plugin_config *config,
    const databind_compiler_service_native_ir *ir, uint32_t contract_version) {
  char client_symbol[512];
  const char *header = plugin_basename(config->client_header_output);
  size_t i;
  if (!plugin_text_valid(header) ||
      !plugin_client_symbol(contract, component, client_symbol, sizeof(client_symbol)) ||
      fputs("#include ", file) == EOF ||
      !plugin_write_c_string(file, header) ||
      fputs("\n#include <cmeta/function.h>\n#include <string.h>\n", file) == EOF)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i)
    if (!databind_compiler_native_contract_emit_reflection(
            file, &ir->operations[i]))
      return 0;
  if (fprintf(file,
        "bool %s_valid(const %s *client) {\n"
        "  if (!client || !client->registry ||\n"
        "      !cmeta_plugin_lease_valid(client->lease)) return false;\n",
        client_symbol, client_symbol) < 0)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i)
    if (fprintf(file,
          "  if (!client->%s_export ||\n"
          "      client->%s_export->kind != CMETA_PLUGIN_EXPORT_FUNCTION ||\n"
          "      !client->%s_export->value.function.invoke) return false;\n",
          ir->operations[i].symbol,
          ir->operations[i].symbol,
          ir->operations[i].symbol) < 0)
      return 0;
  if (fprintf(file,
        "  return true;\n"
        "}\n"
        "static cmeta_plugin_status %s__release(%s *client) {\n"
        "  cmeta_plugin_lease lease;\n"
        "  cmeta_plugin_status status;\n"
        "  if (!client || !client->registry ||\n"
        "      !cmeta_plugin_lease_valid(client->lease))\n"
        "    return CMETA_PLUGIN_INVALID_ARGUMENT;\n"
        "  lease = client->lease;\n"
        "  status = cmeta_plugin_registry_release(client->registry, &lease);\n"
        "  if (status == CMETA_PLUGIN_OK) memset(client, 0, sizeof(*client));\n"
        "  return status;\n"
        "}\n"
        "cmeta_plugin_status %s_open(\n"
        "    cmeta_plugin_registry *registry, cmeta_plugin_ref ref,\n"
        "    %s *out_client) {\n"
        "  cmeta_plugin_lease lease = {0};\n"
        "  const cmeta_plugin_manifest *manifest = NULL;\n"
        "  const cmeta_plugin_export *entry = NULL;\n"
        "  cmeta_plugin_status status, release_status;\n"
        "  if (!out_client) return CMETA_PLUGIN_INVALID_ARGUMENT;\n"
        "  if (out_client->registry || out_client->lease.slot ||\n"
        "      out_client->lease.generation)\n"
        "    return CMETA_PLUGIN_ALREADY;\n"
        "  if (!registry || !cmeta_plugin_ref_valid(ref))\n"
        "    return CMETA_PLUGIN_INVALID_ARGUMENT;\n"
        "  status = cmeta_plugin_registry_acquire(registry, ref, &lease, &manifest);\n"
        "  if (status != CMETA_PLUGIN_OK) return status;\n"
        "  if (!manifest || !manifest->plugin_id ||\n"
        "      strcmp(manifest->plugin_id, ",
        client_symbol, client_symbol,
        client_symbol, client_symbol) < 0 ||
      !plugin_write_c_string(file, component->qualified_name) ||
      fputs(") != 0) {\n"
            "    status = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
            "    goto fail;\n"
            "  }\n", file) == EOF)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (fputs("  entry = NULL;\n"
              "  status = cmeta_plugin_manifest_find_export(manifest, ", file) == EOF ||
        !plugin_write_c_string(file, op->qualified_operation) ||
        fputs(", &entry);\n"
              "  if (status != CMETA_PLUGIN_OK) goto fail;\n"
              "  status = cmeta_plugin_export_require_function(entry, ", file) == EOF ||
        !plugin_write_c_string(file, op->qualified_service) ||
        fprintf(file,
           ", %uu, 0u);\n"
           "  if (status != CMETA_PLUGIN_OK) goto fail;\n"
           "  if (!cmeta_function_desc_equal(entry->value.function.desc,\n"
           "          &%s__function_meta) ||\n"
           "      !cmeta_function_abi_desc_equal(entry->value.function.abi,\n"
           "          &%s__function_abi_meta)) {\n"
           "    status = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;\n"
           "    goto fail;\n"
           "  }\n"
           "  out_client->%s_export = entry;\n",
           contract_version, op->symbol, op->symbol, op->symbol) < 0)
      return 0;
  }
  if (fprintf(file,
        "  out_client->registry = registry;\n"
        "  out_client->lease = lease;\n"
        "  return CMETA_PLUGIN_OK;\n"
        "fail:\n"
        "  memset(out_client, 0, sizeof(*out_client));\n"
        "  out_client->registry = registry;\n"
        "  out_client->lease = lease;\n"
        "  release_status = %s__release(out_client);\n"
        "  return release_status == CMETA_PLUGIN_OK ? status : release_status;\n"
        "}\n"
        "cmeta_plugin_status %s_close(%s *client) {\n"
        "  return %s__release(client);\n"
        "}\n",
        client_symbol, client_symbol, client_symbol, client_symbol) < 0)
    return 0;
  for (i = 0u; i < ir->operation_count; ++i) {
    const databind_compiler_service_native_operation *op = &ir->operations[i];
    if (fprintf(file,
       "cmeta_plugin_status %s_plugin_client_call(\n"
       "    %s *client, const %s *request, %s *response,\n"
       "    int *native_status) {\n"
       "  const cmeta_plugin_export *entry;\n"
       "  void *params[2];\n"
       "  int result;\n"
       "  if (!%s_valid(client) || !request || !response || !native_status)\n"
       "    return CMETA_PLUGIN_INVALID_ARGUMENT;\n"
       "  entry = client->%s_export;\n"
       "  if (!entry || entry->kind != CMETA_PLUGIN_EXPORT_FUNCTION ||\n"
       "      !entry->value.function.invoke)\n"
       "    return CMETA_PLUGIN_INVALID_STATE;\n"
       "  params[0] = (void *)request;\n"
       "  params[1] = response;\n"
       "  if (!entry->value.function.invoke(entry->value.function.context,\n"
       "          &result, params, 2u))\n"
       "    return CMETA_PLUGIN_INVALID_STATE;\n"
       "  *native_status = result;\n"
       "  return CMETA_PLUGIN_OK;\n"
       "}\n",
       op->symbol, client_symbol, op->request_type, op->response_type,
       client_symbol, op->symbol) < 0)
      return 0;
  }
  return 1;
}

static int plugin_generate_impl(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context,
    const char *const stages[4]) {
  const IdlContract *contract = input != NULL ? input->contract : NULL;
  const databind_compiler_plugin_config *config =
      request != NULL
          ? (const databind_compiler_plugin_config *)request->config
          : NULL;
  databind_compiler_service_native_ir native_ir = {0};
  const IdlComponent *component = NULL;
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

  if (contract == NULL || request == NULL ||
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
          contract, &contract_version))
    return -1;

  component = idl_contract_find_component(
      contract, config->component_id);
  if (component == NULL)
    return -1;

  /* Instance-bound application methods cannot be published as free Functions. */
  for (size_t i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    if (plugin_component_has_service(component, service->name) &&
        idl_annotation_count(service->annotations, service->annotation_count, "inject") != 0u)
      return -1;
  }

  if (databind_compiler_service_native_build_selected(
          contract,
          plugin_select_component_service,
          (void *)component,
          &native_ir) != 0)
    goto cleanup;

  if (!plugin_native_ir_valid(
          component, &native_ir,
          &selected_count) ||
      selected_count != native_ir.operation_count)
    goto cleanup;
  /* Binary is never implicitly admitted by a typed Plugin selection.
   * No catalog/codec is exported on the Contract-only Function ABI path. */
  const int contract_only = input->binary_format == NULL &&
      !config->binary_presentation;
  if (contract_only &&
      !databind_compiler_native_contract_admitted(contract, &native_ir))
    goto cleanup;

  if (stages != NULL) {
    size_t i, j;
    const char *finals[] = {
        config->service_header_output, request->output,
        config->client_header_output, config->client_source_output};
    for (i = 0u; i < 4u; ++i) {
      struct stat info;
      if (stages[i] == NULL || stages[i][0] == '\0' ||
          stat(stages[i], &info) != 0 || info.st_size != 0)
        goto cleanup;
#ifdef _WIN32
      if ((info.st_mode & _S_IFMT) != _S_IFREG) goto cleanup;
#else
      if (!S_ISREG(info.st_mode)) goto cleanup;
#endif
      for (j = 0u; j < 4u; ++j)
        if (strcmp(stages[i], finals[j]) == 0) goto cleanup;
      for (j = 0u; j < i; ++j)
        if (strcmp(stages[i], stages[j]) == 0) goto cleanup;
    }
  }

  header_file = stages != NULL
      ? fopen(stages[0], "wb")
      : plugin_open_staging(config->service_header_output, &header_staging);
  if (header_file == NULL) goto cleanup;

  source_file = stages != NULL
      ? fopen(stages[1], "wb")
      : plugin_open_staging(request->output, &source_staging);
  if (source_file == NULL) goto cleanup;

  client_header_file = stages != NULL
      ? fopen(stages[2], "wb")
      : plugin_open_staging(config->client_header_output, &client_header_staging);
  if (client_header_file == NULL) goto cleanup;

  client_source_file = stages != NULL
      ? fopen(stages[3], "wb")
      : plugin_open_staging(config->client_source_output, &client_source_staging);
  if (client_source_file == NULL) goto cleanup;

  if (contract_only
          ? (!plugin_contract_write_header(
                 header_file, contract, component, config, &native_ir) ||
             !plugin_contract_write_provider(
                 source_file, component, config, &native_ir, contract_version) ||
             !plugin_contract_write_client_header(
                 client_header_file, contract, component, config, &native_ir) ||
             !plugin_contract_write_client_source(
                 client_source_file, contract, component, config, &native_ir,
                 contract_version))
          : (!plugin_write_header(
                 header_file, contract, component, config, &native_ir) ||
             !plugin_write_source(
                 source_file, component, config, &native_ir, contract_version) ||
             !plugin_write_client_header(
                 client_header_file, contract, component, config, &native_ir) ||
             !plugin_write_client_source(
                 client_source_file, contract, component, config, &native_ir,
                 contract_version)))
    goto cleanup;

  if (!plugin_close_staging(&header_file) ||
      !plugin_close_staging(&source_file) ||
      !plugin_close_staging(&client_header_file) ||
      !plugin_close_staging(&client_source_file))
    goto cleanup;

  if (stages != NULL) {
    result = 0;
    goto cleanup;
  }

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

int databind_compiler_plugin_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  return plugin_generate_impl(input, request, context, NULL);
}

int databind_compiler_plugin_render_staged(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    const char *provider_header_stage,
    const char *provider_source_stage,
    const char *client_header_stage,
    const char *client_source_stage) {
  const char *stages[] = {
      provider_header_stage, provider_source_stage,
      client_header_stage, client_source_stage};
  return plugin_generate_impl(input, request, NULL, stages);
}

const databind_compiler_projection_backend
    DATABIND_COMPILER_PLUGIN_BACKEND = {
        {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
         DATABIND_COMPILER_ARTIFACT_PLUGIN},
        "plugin",
        databind_compiler_plugin_generate,
        NULL,
        DATABIND_COMPILER_OUTPUT_STAGED_MULTI,
};

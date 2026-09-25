#include "projection_frontend.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int frontend_error(
    char *error, size_t error_size, const char *message) {
  if (error != NULL && error_size != 0u)
    snprintf(error, error_size, "%s", message != NULL ? message : "error");
  return -1;
}

static int frontend_errorf(
    char *error, size_t error_size,
    const char *format, const char *value) {
  if (error != NULL && error_size != 0u)
    snprintf(error, error_size, format,
             value != NULL ? value : "<null>");
  return -1;
}

static int artifact_name_valid(const char *name) {
  size_t i;
  size_t length;
  if (name == NULL || name[0] == '\0') return 0;
  length = strlen(name);
  if (length > DATABIND_COMPILER_ARTIFACT_NAME_MAX ||
      strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return 0;

  for (i = 0u; i < length; ++i) {
    unsigned char ch = (unsigned char)name[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_' || ch == '-' || ch == '.'))
      return 0;
  }
  return 1;
}

static int parse_u32_component(
    const char **cursor, uint32_t *out, char terminal) {
  const unsigned char *p;
  uint64_t value = 0u;
  size_t digits = 0u;

  if (cursor == NULL || *cursor == NULL || out == NULL) return 0;
  p = (const unsigned char *)*cursor;

  while (*p >= '0' && *p <= '9') {
    value = value * 10u + (uint64_t)(*p - '0');
    if (value > UINT32_MAX) return 0;
    ++p;
    ++digits;
  }
  if (digits == 0u || *p != (unsigned char)terminal) return 0;
  *out = (uint32_t)value;
  *cursor = (const char *)(p + (terminal == '\0' ? 0u : 1u));
  return 1;
}

static int parse_version(
    const char *text,
    uint32_t *major,
    uint32_t *minor,
    uint32_t *patch) {
  const char *cursor = text;
  if (text == NULL || major == NULL || minor == NULL || patch == NULL)
    return 0;
  if (!parse_u32_component(&cursor, major, '.')) return 0;
  if (!parse_u32_component(&cursor, minor, '.')) return 0;
  if (!parse_u32_component(&cursor, patch, '\0')) return 0;
  return *cursor == '\0';
}

static int path_reserved(
    const databind_compiler_projection_frontend_input *input,
    const char *path) {
  const char *reserved[] = {
      input->output_path,
      input->source_output_path,
      input->guest_output_path,
      input->dsl_output_path,
      input->projection_config_path,
  };
  size_t i;
  for (i = 0u; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
    if (reserved[i] != NULL && strcmp(reserved[i], path) == 0)
      return 1;
  return 0;
}

static int derive_artifact_path(
    const char *dir,
    const char *artifact_name,
    const char *suffix,
    char *out,
    size_t out_size) {
  char filename[SALTS_FS_MAX_PATH];
  int n;

  n = snprintf(filename, sizeof(filename), "%s%s",
               artifact_name, suffix);
  if (n <= 0 || (size_t)n >= sizeof(filename)) return 0;

  return salts_fs_path_join(
             out, out_size, dir, filename) == 0;
}

static int ensure_artifact_context(
    const databind_compiler_projection_frontend_input *input,
    databind_compiler_projection_frontend_plan *out,
    char *error,
    size_t error_size) {
  if (!artifact_name_valid(input->artifact_name))
    return frontend_error(
        error, error_size,
        "Artifact projections require a safe --artifact-name");
  if (input->output_path == NULL || input->output_path[0] == '\0')
    return frontend_error(
        error, error_size,
        "Artifact projections require --output to anchor generated artifacts");
  if (out->artifact_dir[0] != '\0') return 0;

  if (salts_fs_path_dirname(
          input->output_path, out->artifact_dir,
          sizeof(out->artifact_dir)) != 0 ||
      salts_fs_path_basename(
          input->output_path, out->native_header,
          sizeof(out->native_header)) != 0)
    return frontend_error(
        error, error_size,
        "Unable to derive artifact paths from --output");
  return 0;
}

static int projection_output_in_use(
    const databind_compiler_projection_frontend_plan *out,
    const char *path) {
  size_t i;
  if (out == NULL || path == NULL) return 0;
  for (i = 0u; i < out->request_count; ++i)
    if (out->requests[i].output != NULL &&
        strcmp(out->requests[i].output, path) == 0)
      return 1;
  return 0;
}

static int method_plan_symbol_prefix(
    const char *artifact_name, char *out, size_t out_size) {
  static const char prefix[] = "databind_";
  size_t used = sizeof(prefix) - 1u;
  size_t i;
  if (artifact_name == NULL || out == NULL || out_size <= used + 1u)
    return 0;
  memcpy(out, prefix, used);
  for (i = 0u; artifact_name[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)artifact_name[i];
    if (used + 1u >= out_size) return 0;
    if ((ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '_')
      out[used++] = (char)ch;
    else if (ch == '-' || ch == '.')
      out[used++] = '_';
    else
      return 0;
  }
  if (used == sizeof(prefix) - 1u) return 0;
  out[used] = '\0';
  return 1;
}

static int add_method_plan(
    const databind_compiler_projection_frontend_input *input,
    databind_compiler_projection_frontend_plan *out,
    databind_compiler_projection_kind kind,
    char *error,
    size_t error_size) {
  char *path;
  const char *suffix;
  databind_compiler_projection_backend backend;

  if (ensure_artifact_context(input, out, error, error_size) != 0)
    return -1;
  if (out->method_plan_symbol_prefix[0] == '\0' &&
      !method_plan_symbol_prefix(
          input->artifact_name, out->method_plan_symbol_prefix,
          sizeof(out->method_plan_symbol_prefix)))
    return frontend_error(
        error, error_size,
        "Artifact name cannot form a MethodPlan C symbol prefix");

  if (kind == DATABIND_COMPILER_PROJECTION_HTTP) {
    path = out->http_projection_header;
    suffix = ".http.h";
    backend = databind_compiler_http_method_plan_backend();
  } else if (kind == DATABIND_COMPILER_PROJECTION_RPC) {
    path = out->rpc_projection_header;
    suffix = ".rpc.h";
    backend = databind_compiler_rpc_method_plan_backend();
  } else {
    return frontend_error(
        error, error_size, "Invalid MethodPlan projection kind");
  }

  if (!derive_artifact_path(
          out->artifact_dir, input->artifact_name,
          suffix, path, SALTS_FS_MAX_PATH))
    return frontend_error(
        error, error_size,
        "Derived MethodPlan projection output path is too long");

  if (path_reserved(input, path) || projection_output_in_use(out, path))
    return frontend_error(
        error, error_size,
        "Derived projection outputs collide with another compiler output");

  if (kind == DATABIND_COMPILER_PROJECTION_HTTP) {
    if (out->external_config.has_http)
      out->http = out->external_config.http;
    else
      out->http = (databind_compiler_http_projection_config){0};
    out->http.symbol_prefix = out->method_plan_symbol_prefix;
    out->requests[out->request_count++] =
        (databind_compiler_projection_request){
            .kind = kind, .output = path, .config = &out->http};
  } else {
    if (out->external_config.has_rpc)
      out->rpc = out->external_config.rpc;
    else
      out->rpc = (databind_compiler_rpc_projection_config){0};
    out->rpc.symbol_prefix = out->method_plan_symbol_prefix;
    out->requests[out->request_count++] =
        (databind_compiler_projection_request){
            .kind = kind, .output = path, .config = &out->rpc};
  }
  out->backends[out->backend_count++] = backend;
  return 0;
}

static int token_copy_trimmed(
    const char *begin, const char *end,
    char *out, size_t out_size) {
  size_t length;

  while (begin < end && isspace((unsigned char)*begin)) ++begin;
  while (end > begin && isspace((unsigned char)end[-1])) --end;

  length = (size_t)(end - begin);
  if (length == 0u || length + 1u > out_size) return 0;
  memcpy(out, begin, length);
  out[length] = '\0';
  return 1;
}

static int add_plugin(
    const databind_compiler_projection_frontend_input *input,
    databind_compiler_projection_frontend_plan *out,
    char *error,
    size_t error_size) {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;

  if (ensure_artifact_context(input, out, error, error_size) != 0)
    return -1;
  if (input->component_id == NULL || input->component_id[0] == '\0')
    return frontend_error(
        error, error_size,
        "--projections plugin requires --component <Schema.Component>");
  if (!parse_version(
          input->artifact_version, &major, &minor, &patch))
    return frontend_error(
        error, error_size,
        "--projections plugin requires --artifact-version MAJOR.MINOR.PATCH");
  if (!derive_artifact_path(
          out->artifact_dir, input->artifact_name,
          ".plugin.c",
          out->plugin_source,
          sizeof(out->plugin_source)) ||
      !derive_artifact_path(
          out->artifact_dir, input->artifact_name,
          ".plugin.h",
          out->plugin_service_header,
          sizeof(out->plugin_service_header)) ||
      !derive_artifact_path(
          out->artifact_dir, input->artifact_name,
          ".plugin_client.h",
          out->plugin_client_header,
          sizeof(out->plugin_client_header)) ||
      !derive_artifact_path(
          out->artifact_dir, input->artifact_name,
          ".plugin_client.c",
          out->plugin_client_source,
          sizeof(out->plugin_client_source)))
    return frontend_error(
        error, error_size,
        "Derived projection output path is too long");

  if (strcmp(out->plugin_source, out->plugin_service_header) == 0 ||
      strcmp(out->plugin_source, out->plugin_client_header) == 0 ||
      strcmp(out->plugin_source, out->plugin_client_source) == 0 ||
      strcmp(out->plugin_service_header, out->plugin_client_header) == 0 ||
      strcmp(out->plugin_service_header, out->plugin_client_source) == 0 ||
      strcmp(out->plugin_client_header, out->plugin_client_source) == 0 ||
      path_reserved(input, out->plugin_source) ||
      path_reserved(input, out->plugin_service_header) ||
      path_reserved(input, out->plugin_client_header) ||
      path_reserved(input, out->plugin_client_source) ||
      projection_output_in_use(out, out->plugin_source) ||
      projection_output_in_use(out, out->plugin_service_header) ||
      projection_output_in_use(out, out->plugin_client_header) ||
      projection_output_in_use(out, out->plugin_client_source))
    return frontend_error(
        error, error_size,
        "Derived projection outputs collide with another compiler output");

  out->plugin = (databind_compiler_plugin_config){
      .plugin_version_major = major,
      .plugin_version_minor = minor,
      .plugin_version_patch = patch,
      .component_id = input->component_id,
      .native_header = out->native_header,
      .service_header_output = out->plugin_service_header,
      .client_header_output = out->plugin_client_header,
      .client_source_output = out->plugin_client_source,
  };

  out->requests[out->request_count++] =
      (databind_compiler_projection_request){
          .kind = DATABIND_COMPILER_PROJECTION_PLUGIN,
          .output = out->plugin_source,
          .config = &out->plugin,
      };
  out->backends[out->backend_count++] =
      DATABIND_COMPILER_PLUGIN_BACKEND;
  return 0;
}

int databind_compiler_projection_frontend_build(
    const databind_compiler_projection_frontend_input *input,
    databind_compiler_projection_frontend_plan *out,
    char *error,
    size_t error_size) {
  databind_compiler_projection_kind
      kinds[DATABIND_COMPILER_FRONTEND_MAX_PROJECTIONS];
  size_t kind_count = 0u;
  const char *cursor;
  size_t i;
  int selected_http = 0;
  int selected_rpc = 0;

  if (error != NULL && error_size != 0u) error[0] = '\0';
  if (input == NULL || out == NULL)
    return frontend_error(error, error_size, "Invalid projection frontend input");

  memset(out, 0, sizeof(*out));

  if (input->projections == NULL || input->projections[0] == '\0') {
    if (input->projection_config_path != NULL &&
        input->projection_config_path[0] != '\0')
      return frontend_error(
          error, error_size,
          "--projection-config requires --projections http and/or rpc");
    return 0;
  }

  cursor = input->projections;
  while (*cursor != '\0') {
    const char *end = strchr(cursor, ',');
    char name[64];
    databind_compiler_projection_kind kind;
    size_t j;

    if (end == NULL) end = cursor + strlen(cursor);
    if (!token_copy_trimmed(cursor, end, name, sizeof(name)))
      return frontend_error(
          error, error_size,
          "Projection list contains an empty/invalid name");

    if (databind_compiler_projection_parse(name, &kind) != 0)
      return frontend_errorf(
          error, error_size,
          "Unknown projection '%s'", name);

    for (j = 0u; j < kind_count; ++j)
      if (kinds[j] == kind)
        return frontend_errorf(
            error, error_size,
            "Projection '%s' was selected more than once", name);

    if (kind_count >= DATABIND_COMPILER_FRONTEND_MAX_PROJECTIONS)
      return frontend_error(
          error, error_size,
          "Too many projections selected");

    kinds[kind_count++] = kind;
    if (kind == DATABIND_COMPILER_PROJECTION_HTTP) selected_http = 1;
    if (kind == DATABIND_COMPILER_PROJECTION_RPC) selected_rpc = 1;

    if (*end == ',' && end[1] == '\0')
      return frontend_error(
          error, error_size,
          "Projection list must not end with a comma");

    cursor = *end == ',' ? end + 1 : end;
  }

  if (input->projection_config_path != NULL &&
      input->projection_config_path[0] != '\0') {
    if (!selected_http && !selected_rpc)
      return frontend_error(
          error, error_size,
          "--projection-config is consumed only by HTTP/RPC projections");
    if (databind_compiler_projection_config_load(
            input->projection_config_path, &out->external_config,
            error, error_size) != 0)
      return -1;
    if (out->external_config.has_http && !selected_http) {
      frontend_error(
          error, error_size,
          "Projection config contains http but HTTP is not selected");
      goto fail;
    }
    if (out->external_config.has_rpc && !selected_rpc) {
      frontend_error(
          error, error_size,
          "Projection config contains rpc but RPC is not selected");
      goto fail;
    }
  }

  for (i = 0u; i < kind_count; ++i) {
    switch (kinds[i]) {
    case DATABIND_COMPILER_PROJECTION_PLUGIN:
      if (add_plugin(input, out, error, error_size) != 0)
        goto fail;
      break;
    case DATABIND_COMPILER_PROJECTION_HTTP:
    case DATABIND_COMPILER_PROJECTION_RPC:
      if (add_method_plan(input, out, kinds[i], error, error_size) != 0)
        goto fail;
      break;
    default:
      frontend_errorf(
          error, error_size,
          "Projection '%s' is known but not available from the public frontend yet",
          databind_compiler_projection_name(kinds[i]));
      goto fail;
    }
  }

  if (!databind_compiler_projection_requests_valid(
          out->requests, out->request_count)) {
    frontend_error(error, error_size, "Invalid projection selection");
    goto fail;
  }
  return 0;

fail:
  databind_compiler_projection_frontend_dispose(out);
  return -1;
}

void databind_compiler_projection_frontend_dispose(
    databind_compiler_projection_frontend_plan *plan) {
  if (plan == NULL) return;
  databind_compiler_projection_config_dispose(&plan->external_config);
}

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
      input->lua_output_path,
      input->guest_output_path,
      input->dsl_output_path,
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

  if (input->component_id == NULL || input->component_id[0] == '\0')
    return frontend_error(
        error, error_size,
        "--projections plugin requires --component <Schema.Component>");
  if (!artifact_name_valid(input->artifact_name))
    return frontend_error(
        error, error_size,
        "--projections plugin requires a safe --artifact-name");
  if (!parse_version(
          input->artifact_version, &major, &minor, &patch))
    return frontend_error(
        error, error_size,
        "--projections plugin requires --artifact-version MAJOR.MINOR.PATCH");
  if (input->output_path == NULL || input->output_path[0] == '\0')
    return frontend_error(
        error, error_size,
        "--projections plugin requires --output for the generated C header");

  if (salts_fs_path_dirname(
          input->output_path,
          out->artifact_dir,
          sizeof(out->artifact_dir)) != 0 ||
      salts_fs_path_basename(
          input->output_path,
          out->native_header,
          sizeof(out->native_header)) != 0)
    return frontend_error(
        error, error_size,
        "Unable to derive artifact paths from --output");

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
          ".plugin_client.c",
          out->plugin_client_source,
          sizeof(out->plugin_client_source)))
    return frontend_error(
        error, error_size,
        "Derived projection output path is too long");

  if (strcmp(out->plugin_source, out->plugin_service_header) == 0 ||
      strcmp(out->plugin_source, out->plugin_client_source) == 0 ||
      strcmp(out->plugin_service_header, out->plugin_client_source) == 0 ||
      path_reserved(input, out->plugin_source) ||
      path_reserved(input, out->plugin_service_header) ||
      path_reserved(input, out->plugin_client_source))
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
  const char *cursor;

  if (error != NULL && error_size != 0u) error[0] = '\0';
  if (input == NULL || out == NULL)
    return frontend_error(error, error_size, "Invalid projection frontend input");

  memset(out, 0, sizeof(*out));

  if (input->projections == NULL || input->projections[0] == '\0')
    return 0;

  cursor = input->projections;
  while (*cursor != '\0') {
    const char *end = strchr(cursor, ',');
    char name[64];
    databind_compiler_projection_kind kind;
    size_t i;

    if (end == NULL) end = cursor + strlen(cursor);
    if (!token_copy_trimmed(
            cursor, end, name, sizeof(name)))
      return frontend_error(
          error, error_size,
          "Projection list contains an empty/invalid name");

    if (databind_compiler_projection_parse(name, &kind) != 0)
      return frontend_errorf(
          error, error_size,
          "Unknown projection '%s'", name);

    for (i = 0u; i < out->request_count; ++i)
      if (out->requests[i].kind == kind)
        return frontend_errorf(
            error, error_size,
            "Projection '%s' was selected more than once", name);

    if (out->request_count >=
        DATABIND_COMPILER_FRONTEND_MAX_PROJECTIONS)
      return frontend_error(
          error, error_size,
          "Too many projections selected");

    switch (kind) {
    case DATABIND_COMPILER_PROJECTION_PLUGIN:
      if (add_plugin(input, out, error, error_size) != 0)
        return -1;
      break;
    default:
      return frontend_errorf(
          error, error_size,
          "Projection '%s' is known but not available from the public frontend yet",
          name);
    }

    if (*end == ',' && end[1] == '\0')
      return frontend_error(
          error, error_size,
          "Projection list must not end with a comma");

    cursor = *end == ',' ? end + 1 : end;
  }

  return databind_compiler_projection_requests_valid(
             out->requests, out->request_count)
             ? 0
             : frontend_error(
                   error, error_size,
                   "Invalid projection selection");
}

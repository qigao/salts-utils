#ifndef DATABIND_COMPILER_PROJECTION_FRONTEND_H
#define DATABIND_COMPILER_PROJECTION_FRONTEND_H

#include "plugin_projection.h"
#include "method_plan_projection.h"
#include "projection_config.h"
#include "projection.h"

#include "salts_fs.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATABIND_COMPILER_FRONTEND_MAX_SELECTIONS 11u
#define DATABIND_COMPILER_ARTIFACT_NAME_MAX 127u

typedef struct databind_compiler_projection_frontend_input {
  const char *artifacts;
  const char *transports;
  const char *component_id;
  const char *artifact_name;
  const char *artifact_version;
  const char *projection_config_path;

  /* Native/source-language output selected by the ordinary --output option. */
  const char *output_path;

  /* Other active renderer outputs reserved against generated output collisions. */
  const char *source_output_path;
  const char *guest_output_path;
  const char *dsl_output_path;
} databind_compiler_projection_frontend_input;

typedef struct databind_compiler_projection_frontend_plan {
  databind_compiler_projection_request
      requests[DATABIND_COMPILER_FRONTEND_MAX_SELECTIONS];
  databind_compiler_projection_backend
      backends[DATABIND_COMPILER_FRONTEND_MAX_SELECTIONS];
  size_t request_count;
  size_t backend_count;

  databind_compiler_plugin_config plugin;
  databind_compiler_projection_config external_config;
  databind_compiler_http_projection_config http;
  databind_compiler_rpc_projection_config rpc;

  char method_plan_symbol_prefix[256];
  char artifact_dir[SALTS_FS_MAX_PATH];
  char native_header[SALTS_FS_MAX_PATH];
  char plugin_source[SALTS_FS_MAX_PATH];
  char plugin_service_header[SALTS_FS_MAX_PATH];
  char plugin_client_header[SALTS_FS_MAX_PATH];
  char plugin_client_source[SALTS_FS_MAX_PATH];
  char http_projection_header[SALTS_FS_MAX_PATH];
  char rpc_projection_header[SALTS_FS_MAX_PATH];
} databind_compiler_projection_frontend_plan;

/*
 * Lower public selection inputs into one compiler-private generation request
 * set over one canonical IR.
 *
 * artifacts and transports are independent comma-separated canonical name
 * lists. Known but not-yet-public selections fail explicitly. Format selection
 * remains owned by DataBindFormat/FormatPlan and transport projection config.
 */
int databind_compiler_projection_frontend_build(
    const databind_compiler_projection_frontend_input *input,
    databind_compiler_projection_frontend_plan *out,
    char *error,
    size_t error_size);

void databind_compiler_projection_frontend_dispose(
    databind_compiler_projection_frontend_plan *plan);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_PROJECTION_FRONTEND_H */

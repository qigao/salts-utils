#ifndef DATABIND_COMPILER_PROJECTION_FRONTEND_H
#define DATABIND_COMPILER_PROJECTION_FRONTEND_H

#include "plugin_projection.h"
#include "method_plan_projection.h"
#include "projection.h"

#include "salts_fs.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATABIND_COMPILER_FRONTEND_MAX_PROJECTIONS 7u
#define DATABIND_COMPILER_ARTIFACT_NAME_MAX 127u

typedef struct databind_compiler_projection_frontend_input {
  const char *projections;
  const char *component_id;
  const char *artifact_name;
  const char *artifact_version;

  /* Native/source-language output selected by the ordinary --output option. */
  const char *output_path;

  /* Other active renderer outputs reserved against artifact collisions. */
  const char *source_output_path;
  const char *lua_output_path;
  const char *guest_output_path;
  const char *dsl_output_path;
} databind_compiler_projection_frontend_input;

typedef struct databind_compiler_projection_frontend_plan {
  databind_compiler_projection_request
      requests[DATABIND_COMPILER_FRONTEND_MAX_PROJECTIONS];
  databind_compiler_projection_backend
      backends[DATABIND_COMPILER_FRONTEND_MAX_PROJECTIONS];
  size_t request_count;
  size_t backend_count;

  databind_compiler_plugin_config plugin;

  char artifact_dir[SALTS_FS_MAX_PATH];
  char native_header[SALTS_FS_MAX_PATH];
  char plugin_source[SALTS_FS_MAX_PATH];
  char plugin_service_header[SALTS_FS_MAX_PATH];
  char http_projection_header[SALTS_FS_MAX_PATH];
  char rpc_projection_header[SALTS_FS_MAX_PATH];
} databind_compiler_projection_frontend_plan;

/*
 * Lower public artifact-selection inputs into the compiler-private projection
 * registry.
 *
 * projections is a comma-separated canonical projection-name list.
 * Public backends currently include PLUGIN plus convention-based HTTP/RPC
 * MethodPlan projections. Known but not-yet-public backends fail explicitly
 * rather than silently falling back.
 */
int databind_compiler_projection_frontend_build(
    const databind_compiler_projection_frontend_input *input,
    databind_compiler_projection_frontend_plan *out,
    char *error,
    size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_PROJECTION_FRONTEND_H */

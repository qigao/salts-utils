#ifndef DATABIND_COMPILER_PLUGIN_PROJECTION_H
#define DATABIND_COMPILER_PLUGIN_PROJECTION_H

#include "projection.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compiler-private configuration for the PLUGIN artifact backend.
 *
 * Component grammar is not available yet, so this first slice derives
 * plugin_id from canonical schema identity. Once DataBind Component lands, the
 * backend may derive the default identity from canonical Component IR without
 * changing Service contracts or the generated Plugin ABI.
 */
typedef struct databind_compiler_plugin_config {
  uint32_t plugin_version_major;
  uint32_t plugin_version_minor;
  uint32_t plugin_version_patch;

  /*
   * Phase 1 semantic identity comes from canonical schema/service IR:
   *   plugin_id        = schema_name
   *   contract_version = schema [version(N)]
   *
   * Component grammar may later replace only the plugin_id default.
   */

  /* Existing generated native record header included by the Service header. */
  const char *native_header;

  /* Generated business-facing Service declaration header. */
  const char *service_header_output;
} databind_compiler_plugin_config;

/*
 * Generate one passive Plugin publication source plus one Service declaration
 * header from canonical Service IR.
 *
 * request->output is the generated Plugin .c path.
 */
int databind_compiler_plugin_generate(
    const Node *canonical_ir,
    const databind_compiler_projection_request *request,
    void *context);

extern const databind_compiler_projection_backend
    DATABIND_COMPILER_PLUGIN_BACKEND;

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_PLUGIN_PROJECTION_H */

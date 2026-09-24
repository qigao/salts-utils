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
 * Component selection is mandatory. The backend publishes only Service
 * capabilities referenced by the selected canonical Component.
 */
typedef struct databind_compiler_plugin_config {
  uint32_t plugin_version_major;
  uint32_t plugin_version_minor;
  uint32_t plugin_version_patch;

  /*
   * Canonical qualified Component identity selected from root.components[].
   *
   * Example: Image.ImageProcessor
   *
   * plugin_id = component.qualified_name
   *
   * There is no schema-wide fallback when this field is absent or invalid.
   */
  const char *component_id;

  /* Existing generated native record header included by the Service header. */
  const char *native_header;

  /* Generated business-facing Service declaration header. */
  const char *service_header_output;

  /* Generated host-side typed Plugin client artifacts. */
  const char *client_header_output;
  const char *client_source_output;
} databind_compiler_plugin_config;

/*
 * Generate one passive Plugin publication source plus one Service declaration
 * header from the selected canonical Component's Service capabilities.
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

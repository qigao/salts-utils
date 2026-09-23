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
 * generated_header is the already-generated native DataBind header containing
 * the Request_t / Response_t record declarations.
 *
 * artifact_version is package/build metadata only. Service contract_version is
 * derived separately from schema [version(N)].
 */
typedef struct databind_compiler_plugin_config {
  const char *generated_header;
  uint32_t artifact_version_major;
  uint32_t artifact_version_minor;
  uint32_t artifact_version_patch;
} databind_compiler_plugin_config;

int databind_compiler_plugin_generate(
    const Node *canonical_ir,
    const databind_compiler_projection_request *request,
    void *context);

databind_compiler_projection_backend
databind_compiler_plugin_backend(
    const databind_compiler_plugin_config *config);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_PLUGIN_PROJECTION_H */

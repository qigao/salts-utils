#ifndef DATABIND_COMPILER_NATIVE_SERVICE_PROJECTION_H
#define DATABIND_COMPILER_NATIVE_SERVICE_PROJECTION_H

#include "projection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct databind_compiler_native_service_config {
  /* Basename of the ordinary generated native record header. */
  const char *native_header;
  /* Full output path for the generated Service-native declaration header. */
  const char *header_output;
  /* Explicitly selected Binary wire presentation (including historical
   * stage-only fixtures). Absent means exact Contract-only NativeSourceIR ABI.
   * A selected compiler BinaryFormatPlan independently requires Binary. */
  int binary_presentation;
} databind_compiler_native_service_config;

/* Render into coordinator-reserved, initially absent staging destinations.
 * request->output and config->header_output identify final semantic paths;
 * neither final file is published here. On failure, the coordinator must
 * remove any partially written stages. Caller owns staging and commit. */
int databind_compiler_native_service_render_staged(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    const char *header_stage,
    const char *source_stage);

int databind_compiler_native_service_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context);

extern const databind_compiler_projection_backend
    DATABIND_COMPILER_NATIVE_SERVICE_BACKEND;

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_NATIVE_SERVICE_PROJECTION_H */

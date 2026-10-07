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
} databind_compiler_native_service_config;

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

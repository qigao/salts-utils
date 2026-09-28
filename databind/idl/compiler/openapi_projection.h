#ifndef DATABIND_COMPILER_OPENAPI_PROJECTION_H
#define DATABIND_COMPILER_OPENAPI_PROJECTION_H

#include "method_plan_projection.h"
#include "projection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct databind_compiler_openapi_projection_config {
  const databind_compiler_http_projection_config *http;
} databind_compiler_openapi_projection_config;

extern const databind_compiler_projection_backend
    DATABIND_COMPILER_OPENAPI_BACKEND;

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_OPENAPI_PROJECTION_H */

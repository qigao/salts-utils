#ifndef DATABIND_COMPILER_SOCKET_PLAN_PROJECTION_H
#define DATABIND_COMPILER_SOCKET_PLAN_PROJECTION_H

#include "projection.h"
#include "../runtime/data_bind_socket_plan.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct databind_compiler_socket_projection_config {
  const char *symbol_prefix;
  const char *native_header_include;
  const char *channel_name;
  DataBindFormat format;
  DataBindSocketMode mode;
  DataBindSocketFraming framing;
  size_t max_frame_bytes;
  DataBindPayloadKind payload_kind;
  size_t opaque_max_bytes;
} databind_compiler_socket_projection_config;

databind_compiler_projection_backend
databind_compiler_socket_plan_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_SOCKET_PLAN_PROJECTION_H */

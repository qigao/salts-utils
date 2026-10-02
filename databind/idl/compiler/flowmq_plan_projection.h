#ifndef DATABIND_COMPILER_FLOWMQ_PLAN_PROJECTION_H
#define DATABIND_COMPILER_FLOWMQ_PLAN_PROJECTION_H

#include "projection.h"
#include "../../runtime/data_bind_flowmq_plan.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct databind_compiler_flowmq_projection_config {
  const char *symbol_prefix;
  const char *native_header_include;
  const char *channel_name;
  DataBindFormat format;
  DataBindPayloadKind payload_kind;
  size_t opaque_max_bytes;
  DataBindFlowMQChannelPattern pattern;
  size_t max_payload_bytes;
} databind_compiler_flowmq_projection_config;

databind_compiler_projection_backend
databind_compiler_flowmq_channel_plan_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_FLOWMQ_PLAN_PROJECTION_H */

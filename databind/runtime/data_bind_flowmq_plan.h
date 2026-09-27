#ifndef DATA_BIND_FLOWMQ_PLAN_H
#define DATA_BIND_FLOWMQ_PLAN_H

#include "data_bind.h"
#include "data_bind_native_binding.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION = 1u };

typedef enum DataBindFlowMQChannelPattern {
  DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB = 1,
  DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL = 2
} DataBindFlowMQChannelPattern;

typedef DataBindStatus (*DataBindFlowMQNativeBindingResolverFn)(
    DataBindNativeTypeBinding *out,
    DataBindError *error);

/*
 * Immutable generated Channel delivery contract for FlowMQ.
 *
 * It contains logical contract/representation facts only. Endpoints, routing
 * identities, subscription state, HWM/credit, generation fencing, reconnect,
 * workers and direct CNet ownership remain FlowMQ/application concerns.
 */
typedef struct DataBindFlowMQChannelPlan {
  size_t size;
  uint32_t abi_version;
  const char *channel_name;
  const char *message_type;
  DataBindFormat format;
  DataBindFlowMQChannelPattern pattern;
  size_t max_payload_bytes;
  DataBindFlowMQNativeBindingResolverFn native_binding;
} DataBindFlowMQChannelPlan;

#define DATA_BIND_FLOWMQ_CHANNEL_PLAN_INIT \
  { sizeof(DataBindFlowMQChannelPlan), \
    DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION, \
    NULL, NULL, DATA_BIND_FORMAT_JSON, DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB, \
    0u, NULL }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATA_BIND_FLOWMQ_PLAN_H */

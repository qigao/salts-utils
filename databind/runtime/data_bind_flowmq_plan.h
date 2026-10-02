#ifndef DATA_BIND_FLOWMQ_PLAN_H
#define DATA_BIND_FLOWMQ_PLAN_H

#include "data_bind.h"
#include "data_bind_native_binding.h"
#include "data_bind_opaque_plan.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION = 2u };
enum { DATA_BIND_FLOWMQ_SERVICE_PLAN_ABI_VERSION = 2u };

typedef enum DataBindFlowMQChannelPattern {
  DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB = 1,
  DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL = 2
} DataBindFlowMQChannelPattern;

typedef enum DataBindFlowMQServicePattern {
  DATA_BIND_FLOWMQ_SERVICE_REQ_REP = 1,
  DATA_BIND_FLOWMQ_SERVICE_ROUTER_DEALER = 2
} DataBindFlowMQServicePattern;

typedef DataBindNativeTypeBindingResolverFn DataBindFlowMQNativeBindingResolverFn;

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
  /** Append-only representation class; see DataBindSocketPlan. */
  DataBindPayloadKind payload_kind;
  const DataBindOpaquePlan *opaque_plan;
} DataBindFlowMQChannelPlan;

#define DATA_BIND_FLOWMQ_CHANNEL_PLAN_INIT \
  { sizeof(DataBindFlowMQChannelPlan), \
    DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION, \
    NULL, NULL, DATA_BIND_FORMAT_JSON, DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB, \
    0u, NULL, DATA_BIND_PAYLOAD_FORMAT, NULL }

/*
 * Immutable generated Service request/reply contract for FlowMQ.
 *
 * This plan deliberately does not own Service execution, typed-error envelopes
 * or BindingPlan outcome mapping. Those remain canonical DataBind Service
 * semantics and compose with this transport plan. FlowMQ contributes only the
 * logical messaging pattern and ingress/egress representation identities.
 *
 * request_native_binding/response_native_binding resolve the exact generated
 * CMeta native storage for the canonical Service operation without schema or
 * reflection lookup on the message hot path.
 */
typedef struct DataBindFlowMQServicePlan {
  size_t size;
  uint32_t abi_version;
  const char *service_name;
  const char *operation_name;
  const char *request_type;
  const char *response_type;
  DataBindFormat ingress_format;
  DataBindFormat egress_format;
  DataBindFlowMQServicePattern pattern;
  size_t max_payload_bytes;
  DataBindFlowMQNativeBindingResolverFn request_native_binding;
  DataBindFlowMQNativeBindingResolverFn response_native_binding;
  /** Append-only request/reply payload profiles. */
  DataBindPayloadKind ingress_payload_kind;
  DataBindPayloadKind egress_payload_kind;
  const DataBindOpaquePlan *ingress_opaque_plan;
  const DataBindOpaquePlan *egress_opaque_plan;
} DataBindFlowMQServicePlan;

#define DATA_BIND_FLOWMQ_SERVICE_PLAN_INIT \
  { sizeof(DataBindFlowMQServicePlan), \
    DATA_BIND_FLOWMQ_SERVICE_PLAN_ABI_VERSION, \
    NULL, NULL, NULL, NULL, \
    DATA_BIND_FORMAT_JSON, DATA_BIND_FORMAT_JSON, \
    DATA_BIND_FLOWMQ_SERVICE_REQ_REP, 0u, NULL, NULL, \
    DATA_BIND_PAYLOAD_FORMAT, DATA_BIND_PAYLOAD_FORMAT, NULL, NULL }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATA_BIND_FLOWMQ_PLAN_H */

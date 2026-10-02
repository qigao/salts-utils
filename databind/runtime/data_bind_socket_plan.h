#ifndef DATA_BIND_SOCKET_PLAN_H
#define DATA_BIND_SOCKET_PLAN_H

#include "data_bind.h"
#include "data_bind_native_binding.h"
#include "data_bind_opaque_plan.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_SOCKET_PLAN_ABI_VERSION = 3u };

typedef enum DataBindSocketMode {
  DATA_BIND_SOCKET_MODE_STREAM = 1,
  DATA_BIND_SOCKET_MODE_DATAGRAM = 2
} DataBindSocketMode;

typedef enum DataBindSocketFraming {
  DATA_BIND_SOCKET_FRAMING_NONE = 0,
  DATA_BIND_SOCKET_FRAMING_LENGTH32_BE = 1
} DataBindSocketFraming;

/*
 * Generated resolver for the exact Channel payload native binding.
 *
 * The resolver fills caller-owned metadata from immutable generated CMeta/state
 * tables. It performs no allocation and retains no codec or runtime value.
 */
typedef DataBindNativeTypeBindingResolverFn DataBindSocketNativeBindingResolverFn;

/*
 * Immutable generated Channel delivery plan consumed by CNet/application code.
 *
 * This record owns no socket/session/deployment state. channel_name and
 * message_type are generated static string literals. max_frame_bytes is a
 * compiled codec/publication bound, not a socket buffer-size request.
 */
typedef struct DataBindSocketPlan {
  size_t size;
  uint32_t abi_version;
  const char *channel_name;
  const char *message_type;
  DataBindFormat format;
  DataBindSocketMode mode;
  DataBindSocketFraming framing;
  size_t max_frame_bytes;
  DataBindSocketNativeBindingResolverFn native_binding;
  /** Append-only representation class. FORMAT uses format/native binding;
   * OPAQUE requires format == DATA_BIND_FORMAT_NONE, native_binding == NULL
   * and a valid opaque_plan. */
  DataBindPayloadKind payload_kind;
  const DataBindOpaquePlan *opaque_plan;
} DataBindSocketPlan;

#define DATA_BIND_SOCKET_PLAN_INIT \
  { sizeof(DataBindSocketPlan), DATA_BIND_SOCKET_PLAN_ABI_VERSION, \
    NULL, NULL, DATA_BIND_FORMAT_JSON, DATA_BIND_SOCKET_MODE_STREAM, \
    DATA_BIND_SOCKET_FRAMING_LENGTH32_BE, 0u, NULL, \
    DATA_BIND_PAYLOAD_FORMAT, NULL }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATA_BIND_SOCKET_PLAN_H */

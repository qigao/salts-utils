#ifndef DATA_BIND_SOCKET_PLAN_H
#define DATA_BIND_SOCKET_PLAN_H

#include "data_bind.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_SOCKET_PLAN_ABI_VERSION = 1u };

typedef enum DataBindSocketMode {
  DATA_BIND_SOCKET_MODE_STREAM = 1,
  DATA_BIND_SOCKET_MODE_DATAGRAM = 2
} DataBindSocketMode;

typedef enum DataBindSocketFraming {
  DATA_BIND_SOCKET_FRAMING_NONE = 0,
  DATA_BIND_SOCKET_FRAMING_LENGTH32_BE = 1
} DataBindSocketFraming;

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
} DataBindSocketPlan;

#define DATA_BIND_SOCKET_PLAN_INIT \
  { sizeof(DataBindSocketPlan), DATA_BIND_SOCKET_PLAN_ABI_VERSION, \
    NULL, NULL, DATA_BIND_FORMAT_JSON, DATA_BIND_SOCKET_MODE_STREAM, \
    DATA_BIND_SOCKET_FRAMING_LENGTH32_BE, 0u }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATA_BIND_SOCKET_PLAN_H */

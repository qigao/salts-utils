#ifndef DATA_BIND_CFLOW_H
#define DATA_BIND_CFLOW_H

#include "data_bind_cmeta.h"

#include <cflow/reactive.h>
#include <cflow/stream.h>

#if defined(_WIN32) && defined(DATA_BIND_CFLOW_BUILD_DLL)
  #define DATA_BIND_CFLOW_API __declspec(dllexport)
#else
  #define DATA_BIND_CFLOW_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates a reusable CFlow stream over a borrowed immutable DataBind value.
 *
 * out_stream must be all-bits-zero. The caller destroys it with
 * cflow_stream_destroy(). owner must remain alive through every evaluation and
 * until the stream is destroyed.
 */
DATA_BIND_CFLOW_API DataBindStatus data_bind_cflow_stream_from_value(
    const DataBindValue *owner, DataBindCMetaRangeKind kind, cflow_stream *out_stream);

/**
 * Creates a demand-aware Reactive publisher over a borrowed DataBind value.
 *
 * out_publisher must be all-bits-zero. Subscription moves publisher ownership;
 * owner must remain alive until the publisher is destroyed or the subscription
 * is closed. The publisher buffers no DataBind values.
 */
DATA_BIND_CFLOW_API DataBindStatus data_bind_cflow_publisher_from_value(
    const DataBindValue *owner, DataBindCMetaRangeKind kind,
    cflow_publisher *out_publisher);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_CFLOW_H */

#include "data_bind_cflow.h"

#include <cflow/publishers.h>

#include <string.h>

static int data_bind_cflow_is_zero(const void *value, size_t size) {
  const unsigned char *bytes = (const unsigned char *)value;
  size_t index;
  for (index = 0u; index < size; ++index) {
    if (bytes[index] != 0u) return 0;
  }
  return 1;
}

DataBindStatus data_bind_cflow_stream_from_value(const DataBindValue *owner,
                                                 DataBindCMetaRangeKind kind,
                                                 cflow_stream *out_stream) {
  cmeta_range range = {0};
  DataBindStatus status;

  if (out_stream == NULL || !data_bind_cflow_is_zero(out_stream, sizeof(*out_stream)))
    return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_cmeta_range_init(owner, kind, &range);
  if (status != DATA_BIND_OK) return status;
  if (cflow_stream_from_range(out_stream, range) == NULL) {
    cflow_stream_destroy(out_stream);
    return DATA_BIND_ERR_RUNTIME;
  }
  return DATA_BIND_OK;
}

DataBindStatus data_bind_cflow_publisher_from_value(const DataBindValue *owner,
                                                    DataBindCMetaRangeKind kind,
                                                    cflow_publisher *out_publisher) {
  cmeta_range range = {0};
  DataBindStatus status;

  if (out_publisher == NULL ||
      !data_bind_cflow_is_zero(out_publisher, sizeof(*out_publisher)))
    return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_cmeta_range_init(owner, kind, &range);
  if (status != DATA_BIND_OK) return status;
  if (!cflow_publisher_from_range(out_publisher, range)) return DATA_BIND_ERR_RUNTIME;
  return DATA_BIND_OK;
}

#ifndef DATA_BIND_TEMPORAL_ADAPTER_H
#define DATA_BIND_TEMPORAL_ADAPTER_H

#include "data_bind.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse the existing multi-format temporal syntax into DataBind-owned values.
 *
 * This adapter keeps DateTimeParser implementation types above the DataBind
 * core/package ABI. No parser-owned type escapes these calls.
 */
DataBindStatus data_bind_temporal_parse_datetime(
    const char *text, size_t len, DataBindDateTime *out);
DataBindStatus data_bind_temporal_parse_date(
    const char *text, size_t len, DataBindDate *out);
DataBindStatus data_bind_temporal_parse_time(
    const char *text, size_t len, DataBindTime *out);

/** Convert a DataBind-owned datetime to UTC Unix seconds. */
DataBindStatus data_bind_temporal_to_unix_seconds(
    const DataBindDateTime *value, int64_t *out_seconds);

/** Format a DataBind-owned datetime as RFC 7231/RFC 822 text. */
DataBindStatus data_bind_temporal_format_rfc822(
    const DataBindDateTime *value, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_TEMPORAL_ADAPTER_H */

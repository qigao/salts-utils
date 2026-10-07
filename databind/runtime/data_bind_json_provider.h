#ifndef DATA_BIND_JSON_PROVIDER_H
#define DATA_BIND_JSON_PROVIDER_H

#include "data_bind_format_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the built-in JSON provider.
 *
 * The returned descriptor is immutable and process-lifetime. Passing it to
 * data_bind_format_reader_open() performs explicit JSON selection and
 * data_bind_format_writer_open() streams compact JSON to the caller-owned byte
 * sink. No registry lookup, fallback, DOM egress materialization or transport
 * dependency is involved.
 */
DATA_BIND_API const DataBindFormatProvider *data_bind_json_format_provider(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_JSON_PROVIDER_H */

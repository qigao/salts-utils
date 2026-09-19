#ifndef DATA_BIND_JSON_PROVIDER_H
#define DATA_BIND_JSON_PROVIDER_H

#include "data_bind_format_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the statically linked JSON provider.
 *
 * The returned descriptor is immutable and process-lifetime. Passing it to
 * data_bind_format_reader_open() performs explicit JSON selection; no registry
 * lookup or fallback is involved.
 */
const DataBindFormatProvider *data_bind_json_format_provider(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_JSON_PROVIDER_H */

#ifndef TBE_SCHEMA_CMETA_H
#define TBE_SCHEMA_CMETA_H

#include <cmeta/data.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Resolve a schema builtin scalar name to the canonical Salts Core CMeta
 * descriptor. Returns NULL when the name is not a builtin with a canonical
 * descriptor.
 *
 * Integer aliases always resolve to exact-width descriptors. UUID resolves to
 * the process-wide salts_uuid_cmeta_data descriptor. No DataBind-private
 * descriptor is created by this API.
 */
const cmeta_data_desc *schema_cmeta_builtin_data(const char *name);

#ifdef __cplusplus
}
#endif

#endif

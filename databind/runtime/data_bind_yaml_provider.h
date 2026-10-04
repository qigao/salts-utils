#ifndef DATA_BIND_YAML_PROVIDER_H
#define DATA_BIND_YAML_PROVIDER_H

#include "data_bind_format_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the statically linked YAML provider.
 *
 * YAML input is parsed by CYaml, converted through the existing loss-checked
 * CYaml/JSON adapter, then exposed as canonical CSerde tokens.
 *
 * The same provider also accepts the JSON-compatible CSerde token domain for
 * egress and builds one owned CYaml document directly before emission. No JSON
 * text round-trip or alternate format fallback is performed. CSERDE_BYTES and
 * non-finite floating values fail explicitly because they have no canonical
 * YAML profile in this adapter.
 */
const DataBindFormatProvider *data_bind_yaml_format_provider(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_YAML_PROVIDER_H */

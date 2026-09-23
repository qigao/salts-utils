#ifndef DATA_BIND_YAML_PROVIDER_H
#define DATA_BIND_YAML_PROVIDER_H

#include "data_bind_format_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the statically linked YAML provider.
 *
 * YAML is parsed by CYaml, converted through the existing loss-checked
 * CYaml/JSON adapter, then exposed as canonical CSerde tokens. YAML constructs
 * that cannot be represented without loss fail; they never fall back to a
 * different parser or representation.
 */
const DataBindFormatProvider *data_bind_yaml_format_provider(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_YAML_PROVIDER_H */

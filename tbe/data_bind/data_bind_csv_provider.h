#ifndef DATA_BIND_CSV_PROVIDER_H
#define DATA_BIND_CSV_PROVIDER_H

#include "data_bind_format_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the statically linked CSV provider.
 *
 * The canonical token shape is an array of row maps. The first logical record
 * is the header and supplies each map key; every cell value is emitted as a
 * byte-counted CSerde string so schema-owned conversion remains above the CSV
 * syntax layer. Header and cell views borrow the provider-owned CSV document.
 */
const DataBindFormatProvider *data_bind_csv_format_provider(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_CSV_PROVIDER_H */

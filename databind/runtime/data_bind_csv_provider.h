#ifndef DATA_BIND_CSV_PROVIDER_H
#define DATA_BIND_CSV_PROVIDER_H

#include "data_bind_format_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the built-in CSV provider.
 *
 * The canonical token shape is an array of row maps. The first logical record
 * is the header and supplies each map key; every cell value is emitted as a
 * byte-counted CSerde string so schema-owned conversion remains above the CSV
 * syntax layer. Header and cell views borrow the provider-owned CSV document.
 */
DATA_BIND_API const DataBindFormatProvider *data_bind_csv_format_provider(void);

/**
 * Open exactly one zero-based CSV data row as a canonical row MAP reader.
 *
 * The CSV header is not counted as a data row. Empty cells are omitted from
 * the MAP so MessagePlan observes them as ABSENT, matching the historical
 * generated from_csv(row) contract. A row outside the parsed data-row range,
 * or a ragged row that does not match its header, fails with
 * DATA_BIND_ERR_TYPE_MISMATCH.
 *
 * The returned lease is closed with data_bind_format_reader_close().
 * This API performs no schema lookup or field-name canonicalization; callers
 * that need schema aliases should wrap the reader with a compiled FormatPlan
 * canonical reader.
 */
DATA_BIND_API DataBindStatus data_bind_csv_format_reader_open_row(
    const char *data,
    size_t len,
    size_t row,
    size_t max_depth,
    DataBindFormatReader *out_reader,
    DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_CSV_PROVIDER_H */

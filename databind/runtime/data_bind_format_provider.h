#ifndef DATA_BIND_FORMAT_PROVIDER_H
#define DATA_BIND_FORMAT_PROVIDER_H

#include "data_bind.h"

#include <cserde/reader.h>
#include <cserde/writer.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_FORMAT_PROVIDER_ABI_VERSION = 1u };

typedef struct DataBindFormatProvider DataBindFormatProvider;

typedef DataBindStatus (*DataBindFormatReaderOpenFn)(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error);

typedef void (*DataBindFormatReaderCloseFn)(
    cserde_reader *reader,
    void *owner);

typedef DataBindStatus (*DataBindFormatReaderOpenSelectedFn)(
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindStreamSelection selection,
    const char *path,
    const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error);

typedef DataBindStatus (*DataBindFormatWriterOpenFn)(
    DataBindWriteFn write,
    void *write_user,
    size_t max_depth,
    cserde_writer **out_writer,
    void **out_owner,
    DataBindError *error);

typedef DataBindStatus (*DataBindFormatWriterCloseFn)(
    cserde_writer *writer,
    void *owner,
    DataBindError *error);

struct DataBindFormatProvider {
  size_t size;
  uint32_t abi_version;
  DataBindFormat format;
  DataBindFormatReaderOpenFn open_reader;
  DataBindFormatReaderCloseFn close_reader;
  /** Optional append-only v1 extension for format-owned selection/query. */
  DataBindFormatReaderOpenSelectedFn open_selected_reader;
  /** Optional append-only v1 extension for streaming format egress. */
  DataBindFormatWriterOpenFn open_writer;
  DataBindFormatWriterCloseFn close_writer;
};

typedef struct DataBindFormatReader {
  size_t size;
  const DataBindFormatProvider *provider;
  cserde_reader *reader;
  void *owner;
} DataBindFormatReader;

typedef struct DataBindFormatWriter {
  size_t size;
  const DataBindFormatProvider *provider;
  cserde_writer *writer;
  void *owner;
} DataBindFormatWriter;

#define DATA_BIND_FORMAT_PROVIDER_INIT(format_, open_, close_) \
  { sizeof(DataBindFormatProvider), DATA_BIND_FORMAT_PROVIDER_ABI_VERSION, \
    (format_), (open_), (close_), NULL, NULL, NULL }

#define DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_INIT( \
    format_, open_, close_, selected_) \
  { sizeof(DataBindFormatProvider), DATA_BIND_FORMAT_PROVIDER_ABI_VERSION, \
    (format_), (open_), (close_), (selected_), NULL, NULL }

#define DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_AND_WRITER_INIT( \
    format_, open_, close_, selected_, writer_open_, writer_close_) \
  { sizeof(DataBindFormatProvider), DATA_BIND_FORMAT_PROVIDER_ABI_VERSION, \
    (format_), (open_), (close_), (selected_), (writer_open_), (writer_close_) }

#define DATA_BIND_FORMAT_READER_INIT \
  { sizeof(DataBindFormatReader), NULL, NULL, NULL }

#define DATA_BIND_FORMAT_WRITER_INIT \
  { sizeof(DataBindFormatWriter), NULL, NULL, NULL }

/**
 * Open one explicit format provider as a CSerde reader.
 *
 * The provider is supplied directly by the caller. DataBind performs no
 * registry lookup, plugin discovery, fallback, or format substitution.
 *
 * On success, out_reader owns one provider lease until
 * data_bind_format_reader_close() is called. The provider owns all parser/DOM
 * state behind owner and must keep any token views valid according to the
 * cserde_reader contract.
 */
DataBindStatus data_bind_format_reader_open(
    const DataBindFormatProvider *provider,
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindFormatReader *out_reader,
    DataBindError *error);

/**
 * Open one explicit provider with provider-owned selection/query semantics.
 *
 * PATH_* selections require a non-empty path. ROOT/ALL reject a non-empty
 * path. Query limits/diagnostics are DataBind-owned records; concrete QueryVM
 * types never cross this boundary. Providers that do not implement selection
 * fail with DATA_BIND_ERR_INVALID_ARG rather than falling back to another
 * provider or parser.
 */
DataBindStatus data_bind_format_reader_open_selected(
    const DataBindFormatProvider *provider,
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindStreamSelection selection,
    const char *path,
    const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic,
    DataBindFormatReader *out_reader,
    DataBindError *error);

/**
 * Close an explicit provider lease. A zero/closed lease is accepted.
 */
DataBindStatus data_bind_format_reader_close(DataBindFormatReader *reader);

/**
 * Open one explicit format provider as a streaming CSerde writer.
 *
 * The caller owns the byte sink and any retained output storage. Providers
 * without writer capability fail explicitly; no format fallback is selected.
 */
DataBindStatus data_bind_format_writer_open(
    const DataBindFormatProvider *provider,
    DataBindWriteFn write,
    void *write_user,
    size_t max_depth,
    DataBindFormatWriter *out_writer,
    DataBindError *error);

/**
 * Finish and close one writer lease. Provider finish/sink errors are returned.
 * A zero/closed lease is accepted.
 */
DataBindStatus data_bind_format_writer_close(
    DataBindFormatWriter *writer,
    DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_FORMAT_PROVIDER_H */

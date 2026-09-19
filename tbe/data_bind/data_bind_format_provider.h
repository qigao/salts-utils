#ifndef DATA_BIND_FORMAT_PROVIDER_H
#define DATA_BIND_FORMAT_PROVIDER_H

#include "data_bind.h"

#include <cserde/reader.h>

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

struct DataBindFormatProvider {
  size_t size;
  uint32_t abi_version;
  DataBindFormat format;
  DataBindFormatReaderOpenFn open_reader;
  DataBindFormatReaderCloseFn close_reader;
};

typedef struct DataBindFormatReader {
  size_t size;
  const DataBindFormatProvider *provider;
  cserde_reader *reader;
  void *owner;
} DataBindFormatReader;

#define DATA_BIND_FORMAT_PROVIDER_INIT(format_, open_, close_) \
  { sizeof(DataBindFormatProvider), DATA_BIND_FORMAT_PROVIDER_ABI_VERSION, \
    (format_), (open_), (close_) }

#define DATA_BIND_FORMAT_READER_INIT \
  { sizeof(DataBindFormatReader), NULL, NULL, NULL }

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
 * Close an explicit provider lease. A zero/closed lease is accepted.
 */
DataBindStatus data_bind_format_reader_close(DataBindFormatReader *reader);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_FORMAT_PROVIDER_H */

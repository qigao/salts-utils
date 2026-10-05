#ifndef DATA_BIND_VALUE_READER_H
#define DATA_BIND_VALUE_READER_H

#include "data_bind.h"
#include <cserde/reader.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  DATA_BIND_VALUE_READER_ABI_VERSION = 1u,
  DATA_BIND_VALUE_READER_DEFAULT_DEPTH = 64u,
  DATA_BIND_VALUE_READER_DEFAULT_ITEMS = 65536u,
  DATA_BIND_VALUE_READER_DEFAULT_VIEW_BYTES = 16u * 1024u * 1024u
};
typedef struct DataBindValueReaderLimits {
  size_t size;
  uint32_t abi_version;
  size_t max_depth;
  size_t max_items;
  size_t max_view_bytes;
} DataBindValueReaderLimits;
#define DATA_BIND_VALUE_READER_LIMITS_INIT \
  { sizeof(DataBindValueReaderLimits), DATA_BIND_VALUE_READER_ABI_VERSION, \
    DATA_BIND_VALUE_READER_DEFAULT_DEPTH, DATA_BIND_VALUE_READER_DEFAULT_ITEMS, \
    DATA_BIND_VALUE_READER_DEFAULT_VIEW_BYTES }

/**
 * Open a canonical CSerde reader over one borrowed immutable value tree.
 * value and its complete tree must remain alive and unchanged until close.
 * Operations and close are single-threaded. The reader owns only a fixed stack
 * allocated at open; it neither retains nor frees value. Slices are STABLE
 * until close, within the source lifetime.
 *
 * NULL, bool, exact signed/unsigned integers, double, string, bytes and UUID
 * (16 BYTES) are admitted. Objects emit MAP with canonical names, lists/sets
 * emit ARRAY in stored order, and maps preserve actual typed keys. Other scalar
 * domains return CSERDE_UNSUPPORTED without textual conversion. No schema
 * lookup, format parsing or native storage layout is involved.
 *
 * limits may be NULL for defaults. max_depth bounds simultaneously open
 * containers (zero admits scalar roots only). Nonzero max_items counts value
 * nodes including containers and map keys, excluding object names/end tokens.
 * max_view_bytes bounds cumulative slice lengths including object names;
 * zero admits empty slices only. Traversal budget exhaustion returns sticky
 * CSERDE_LIMIT_EXCEEDED from cserde_reader_next.
 *
 * out_reader is required and set to NULL on failure. Invalid arguments/limit
 * ABI return DATA_BIND_ERR_INVALID_ARG, stack overflow DATA_BIND_ERR_LIMIT,
 * allocation failure DATA_BIND_ERR_OOM. error is optional; traversal errors
 * are reported by cserde_reader_next. Time O(nodes + view bytes), owned space
 * O(max_depth). Pass the reader to data_bind_message_plan_decode_native with
 * fresh staging, close it, then publish only on success. Native CMeta providers
 * own the copied buffers; the source can be freed after close.
 */
DATA_BIND_API DataBindStatus data_bind_value_reader_open(
    const DataBindValue *value, const DataBindValueReaderLimits *limits,
    cserde_reader **out_reader, DataBindError *error);

/** Free a reader returned by open; NULL is allowed. Call exactly once. */
DATA_BIND_API void data_bind_value_reader_close(cserde_reader *reader);

#ifdef __cplusplus
}
#endif
#endif

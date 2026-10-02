#ifndef DATA_BIND_NATIVE_INTERNAL_H
#define DATA_BIND_NATIVE_INTERNAL_H

#include "data_bind_native.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read one already-admitted canonical native leaf into a borrowed CSerde token.
 *
 * This helper performs no descriptor-graph traversal or schema/reflection
 * lookup. Callers must admit the descriptor at compile/preflight time.
 */
DataBindStatus data_bind_native_leaf_token(
    const cmeta_data_desc *data, const void *source, cserde_token *out);


/* Runtime usage reported by one successful native decode. */
typedef struct DataBindNativeDecodeUsage {
  size_t items;
  size_t owned_bytes;
} DataBindNativeDecodeUsage;

/*
 * Decode one canonical native value and publish exact runtime budget usage.
 *
 * This is an internal composition seam for MessagePlan. Public native decode
 * ABI remains unchanged. usage is written only on success.
 */
DataBindStatus data_bind_native_decode_usage(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    cserde_reader *reader, void *destination, size_t destination_bytes,
    DataBindNativeDecodeUsage *usage,
    DataBindNativeDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_NATIVE_INTERNAL_H */

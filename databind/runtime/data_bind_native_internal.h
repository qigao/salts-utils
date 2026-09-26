#ifndef DATA_BIND_NATIVE_INTERNAL_H
#define DATA_BIND_NATIVE_INTERNAL_H

#include "data_bind_native.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DataBindNativeDecodeUsage {
  size_t items;
  size_t owned_bytes;
} DataBindNativeDecodeUsage;

/*
 * Internal continuation entry for callers that must inspect one forward-only
 * CSerde token before delegating the complete VALUE to the canonical native
 * decoder.
 *
 * first_token is the already-read first token of exactly one value. reader is
 * left positioned immediately after that token.
 *
 * usage is an optional cumulative input/output counter. Its incoming values
 * are charged before this value; successful decode publishes the new cumulative
 * totals. Failure leaves usage unchanged. This lets MessagePlan preserve one
 * whole-message max_items/max_owned_bytes budget across multiple exact field
 * decodes without weakening native descriptor preflight.
 *
 * Public data_bind_native_decode* semantics are unchanged and route through the
 * same implementation with zero initial usage and no first token.
 */
DataBindStatus data_bind_native_decode_from_token_internal(
    const DataBindNativeOptions *options,
    const cmeta_data_desc *shape,
    cserde_reader *reader,
    const cserde_token *first_token,
    void *destination,
    size_t destination_bytes,
    size_t max_buffer_bytes,
    DataBindNativeDecodeUsage *usage,
    DataBindNativeDiagnostic *diagnostic);

/*
 * Read one already-admitted canonical native leaf into a borrowed CSerde token.
 *
 * This helper performs no descriptor-graph traversal or schema/reflection
 * lookup. Callers must admit the descriptor at compile/preflight time.
 */
DataBindStatus data_bind_native_leaf_token(
    const cmeta_data_desc *data, const void *source, cserde_token *out);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_NATIVE_INTERNAL_H */

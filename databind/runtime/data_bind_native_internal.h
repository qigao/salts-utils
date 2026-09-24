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

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_NATIVE_INTERNAL_H */

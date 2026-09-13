#ifndef TBE_TYPED_INTERNAL_H
#define TBE_TYPED_INTERNAL_H

#include "tbe_typed.h"
#include <cmeta/data.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal migration seam: map canonical CMeta scalar storage semantics onto
 * the existing typed runtime kind without introducing a second type identity. */
DATA_BIND_API int tbe_typed_kind_from_cmeta_data(const cmeta_data_desc *data,
                                                 TbeTypedKind *out_kind);

/* #45 internal graph validation. Borrows immutable metadata for the call;
 * unsupported storage returns SCHEMA without invoking a binding fallback.
 * Wire names, presence, defaults and fingerprint stay in the schema overlay. */
DATA_BIND_API DataBindStatus tbe_typed_cmeta_graph_validate(
    const TbeTypedType *type, const cmeta_data_desc *data, DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* TBE_TYPED_INTERNAL_H */

#ifndef TBE_SCHEMA_CMETA_BUFFER_H
#define TBE_SCHEMA_CMETA_BUFFER_H

#include <cmeta/data.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal, non-installed STRING/BYTES storage descriptor builder for #45.
 *
 * The caller explicitly supplies a storage type, owned/borrowed buffer shape
 * and matching provider operations. All input metadata, including names, is
 * borrowed: it must remain immutable and outlive every use of the descriptor.
 * Construction neither allocates runtime storage nor invokes provider callbacks.
 *
 * Returns nonzero after CMeta validates the mapping. Invalid inputs, non-buffer
 * kinds and custom ownership return zero without changing *out_data. Runtime
 * buffer lifecycle belongs to the selected CMeta provider; this builder does
 * not choose or substitute storage.
 */
int schema_cmeta_buffer_data(cmeta_data_desc *out_data,
                             const char *stable_id,
                             const char *display_name,
                             cmeta_data_kind kind,
                             const cmeta_type_desc *storage_type,
                             const cmeta_data_buffer_shape *shape,
                             const cmeta_data_buffer_ops *ops);

#ifdef __cplusplus
}
#endif

#endif /* TBE_SCHEMA_CMETA_BUFFER_H */

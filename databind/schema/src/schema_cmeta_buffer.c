#include "schema_cmeta_buffer.h"

int schema_cmeta_buffer_data(cmeta_data_desc *out_data,
                             const char *stable_id,
                             const char *display_name,
                             cmeta_data_kind kind,
                             const cmeta_type_desc *storage_type,
                             const cmeta_data_buffer_shape *shape,
                             const cmeta_data_buffer_ops *ops) {
    const cmeta_data_desc data = {
        sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
        stable_id, display_name, kind, storage_type, shape, ops, NULL, NULL
    };

    if (out_data == NULL ||
        (kind != CMETA_DATA_STRING && kind != CMETA_DATA_BYTES) ||
        cmeta_data_buffer_ops_of(&data) == NULL ||
        shape->ownership == CMETA_DATA_BUFFER_CUSTOM)
        return 0;

    *out_data = data;
    return 1;
}

#ifndef TBE_SCHEMA_BUILTIN_TYPE_H
#define TBE_SCHEMA_BUILTIN_TYPE_H

#include "schema_cmeta.h"
#include <salts_cmeta_fixed_width.h>

#include <stddef.h>
#include <string.h>

typedef struct schema_builtin_type_info {
    const char *name;
    size_t size;
    const char *wire_reader;
    const char *host_type;
    unsigned char is_integer;
    unsigned char is_unsigned;
    unsigned char is_float;
    const cmeta_data_desc *data;
} schema_builtin_type_info_t;

/* Schema owns wire octets/readers/host projections, not scalar alias identity.
 * Keep one profile per canonical scalar; aliases are resolved only by CMeta.
 * Existing classification fields stay for the #45 consumer migration and are
 * checked against the canonical kind by the production-profile regression. */
static const schema_builtin_type_info_t SCHEMA_BUILTIN_TYPES[] = {
    {"bool", 1, "u8", "uint8_t", 0, 0, 0, &cmeta_data_bool},
    {"uint8_t", 1, "u8", "uint8_t", 1, 1, 0, &salts_uint8_cmeta_data},
    {"int8_t", 1, "i8", "int8_t", 1, 0, 0, &salts_int8_cmeta_data},
    {"uint16_t", 2, "u16", "uint16_t", 1, 1, 0, &salts_uint16_cmeta_data},
    {"int16_t", 2, "i16", "int16_t", 1, 0, 0, &salts_int16_cmeta_data},
    {"uint32_t", 4, "u32", "uint32_t", 1, 1, 0, &salts_uint32_cmeta_data},
    {"int32_t", 4, "i32", "int32_t", 1, 0, 0, &salts_int32_cmeta_data},
    {"uint64_t", 8, "u64", "uint64_t", 1, 1, 0, &salts_uint64_cmeta_data},
    {"int64_t", 8, "i64", "int64_t", 1, 0, 0, &salts_int64_cmeta_data},
    {"float", 4, "f32", "float", 0, 0, 1, &cmeta_data_float},
    {"double", 8, "f64", "double", 0, 0, 1, &cmeta_data_double},
};

static inline const schema_builtin_type_info_t *schema_builtin_type_find(const char *name) {
    const cmeta_data_desc *data = schema_cmeta_builtin_data(name);
    size_t i;

    if (!cmeta_data_desc_valid(data)) return NULL;
    for (i = 0; i < sizeof(SCHEMA_BUILTIN_TYPES) / sizeof(SCHEMA_BUILTIN_TYPES[0]); ++i) {
        const schema_builtin_type_info_t *profile = &SCHEMA_BUILTIN_TYPES[i];
        if (data->kind == profile->data->kind &&
            cmeta_type_identity_equal(data->storage_type->identity,
                                      profile->data->storage_type->identity))
            return profile;
    }
    /* A canonical descriptor alone does not grant a numeric wire projection
     * (UUID), or permission to select STRING/BYTES storage. */
    return NULL;
}

#endif

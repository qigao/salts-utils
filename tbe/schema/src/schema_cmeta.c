#include "schema_cmeta.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <string.h>

typedef struct schema_cmeta_builtin_entry {
    const char *name;
    const cmeta_data_desc *data;
} schema_cmeta_builtin_entry_t;

#define SCHEMA_CMETA_ENTRY(name_, data_) {name_, &(data_)}

static const schema_cmeta_builtin_entry_t SCHEMA_CMETA_BUILTINS[] = {
    SCHEMA_CMETA_ENTRY("int8_t", salts_int8_cmeta_data),
    SCHEMA_CMETA_ENTRY("int8", salts_int8_cmeta_data),
    SCHEMA_CMETA_ENTRY("i8", salts_int8_cmeta_data),
    SCHEMA_CMETA_ENTRY("int16_t", salts_int16_cmeta_data),
    SCHEMA_CMETA_ENTRY("int16", salts_int16_cmeta_data),
    SCHEMA_CMETA_ENTRY("i16", salts_int16_cmeta_data),
    SCHEMA_CMETA_ENTRY("int32_t", salts_int32_cmeta_data),
    SCHEMA_CMETA_ENTRY("int32", salts_int32_cmeta_data),
    SCHEMA_CMETA_ENTRY("i32", salts_int32_cmeta_data),
    SCHEMA_CMETA_ENTRY("int64_t", salts_int64_cmeta_data),
    SCHEMA_CMETA_ENTRY("int64", salts_int64_cmeta_data),
    SCHEMA_CMETA_ENTRY("i64", salts_int64_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint8_t", salts_uint8_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint8", salts_uint8_cmeta_data),
    SCHEMA_CMETA_ENTRY("u8", salts_uint8_cmeta_data),
    SCHEMA_CMETA_ENTRY("byte", salts_uint8_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint16_t", salts_uint16_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint16", salts_uint16_cmeta_data),
    SCHEMA_CMETA_ENTRY("u16", salts_uint16_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint32_t", salts_uint32_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint32", salts_uint32_cmeta_data),
    SCHEMA_CMETA_ENTRY("u32", salts_uint32_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint64_t", salts_uint64_cmeta_data),
    SCHEMA_CMETA_ENTRY("uint64", salts_uint64_cmeta_data),
    SCHEMA_CMETA_ENTRY("u64", salts_uint64_cmeta_data),
    SCHEMA_CMETA_ENTRY("uuid", salts_uuid_cmeta_data),
};

#undef SCHEMA_CMETA_ENTRY

const cmeta_data_desc *schema_cmeta_builtin_data(const char *name) {
    size_t i;

    if (name == NULL) return NULL;
    for (i = 0; i < sizeof(SCHEMA_CMETA_BUILTINS) / sizeof(SCHEMA_CMETA_BUILTINS[0]); ++i) {
        if (strcmp(name, SCHEMA_CMETA_BUILTINS[i].name) == 0)
            return SCHEMA_CMETA_BUILTINS[i].data;
    }
    return NULL;
}

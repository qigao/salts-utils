#include "data_bind.h"

/* C and C++ consumers both link a separately C-compiled DataBind library.
 * A missing extern-C/export or a failed-query publication makes this fail. */
int main(void) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc sentinel = {0};
    const cmeta_data_desc *out = &sentinel;
    field.has_cmeta_kind = 1;
    field.cmeta_kind = CMETA_DATA_SEQUENCE;
    field.cmeta_data = &sentinel;
    if (data_bind_schema_field_cmeta_data(NULL, "Shape", 0, &out, &error) != DATA_BIND_ERR_INVALID_ARG)
        return 1;
    if (out != &sentinel || error.code != DATA_BIND_ERR_INVALID_ARG) return 2;
    return field.cmeta_kind != CMETA_DATA_SEQUENCE;
}

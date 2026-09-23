#include "data_bind.h"

/* C and C++ consumers both link a separately C-compiled DataBind library.
 * A missing extern-C/export or a failed-query publication makes this fail. */
int main(void) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindService service = DATA_BIND_SERVICE_INIT;
    DataBindServiceOperation operation = DATA_BIND_SERVICE_OPERATION_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc sentinel = {0};
    const cmeta_data_desc *out = &sentinel;
    field.has_cmeta_kind = 1;
    field.cmeta_kind = CMETA_DATA_SEQUENCE;
    field.cmeta_data = &sentinel;
    if (data_bind_schema_field_cmeta_data(NULL, "Shape", 0, &out, &error) != DATA_BIND_ERR_INVALID_ARG)
        return 1;
    if (out != &sentinel || error.code != DATA_BIND_ERR_INVALID_ARG) return 2;
    if (service.size != sizeof(DataBindService)) return 3;
    if (operation.size != sizeof(DataBindServiceOperation)) return 4;
    if (data_bind_service_at(NULL, 0u, &service) != 0) return 5;
    if (data_bind_service_operation_at(NULL, "Service", 0u, &operation) != 0)
        return 6;
    return field.cmeta_kind != CMETA_DATA_SEQUENCE;
}

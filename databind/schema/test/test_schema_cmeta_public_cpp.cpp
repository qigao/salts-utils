#include "schema_cmeta.h"

#include <cstring>

int main() {
    const IdlContract contract = {
        sizeof(IdlContract), IDL_CONTRACT_ABI_VERSION,
        "Typed", "1", 0u, nullptr, 0u, nullptr, 0u, nullptr, 0u, nullptr};
    const IdlField field = {
        "value", "int32", IDL_COLLECTION_NONE,
        nullptr, nullptr, nullptr, nullptr, nullptr,
        0, 0, 0u, nullptr, 0u, nullptr};
    schema_cmeta_field_type resolved = {};

    if (!schema_cmeta_field_resolve(&contract, &field, &resolved)) return 1;
    if (resolved.kind != CMETA_DATA_SINT || resolved.data == nullptr) return 2;
    if (std::strcmp(resolved.data->stable_id, cmeta_data_int32.stable_id) != 0) return 3;
    return 0;
}

#include "native_owned_value_fixture.h"
#include "data_bind_native.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    OwnedValue_native_cmeta_binding meta = {0};
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativePlan *plan = NULL;
    OwnedValue source = {0}, clone = {0}, moved = {0};
    unsigned char workspace[16384] = {0};
    const unsigned char bytes[] = {3u, 7u, 9u};
    bool zero = false;
    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);
    options.max_depth = 16u;
    options.max_items = 128u;
    options.max_owned_bytes = 4096u;

    if (OwnedValue_native_cmeta_value_bind(&meta) != 0 ||
        !cmeta_data_desc_valid(&meta.data) ||
        meta.reflection.mode != CMETA_DATA_REFLECTION_VALUE ||
        !cmeta_data_value_traits_supported(&meta.data) ||
        !cmeta_data_desc_valid(&databind_native_text_cmeta_data) ||
        !cmeta_data_desc_valid(&databind_native_bytes_cmeta_data))
        return 1;
    if (data_bind_native_plan_compile(
            &options, &meta.data, &plan, &diagnostic) != DATA_BIND_OK ||
        plan == NULL)
        return 2;
    if (data_bind_native_plan_init(
            plan, &options, &source, sizeof(source), &diagnostic) != DATA_BIND_OK ||
        data_bind_native_plan_init(
            plan, &options, &clone, sizeof(clone), &diagnostic) != DATA_BIND_OK ||
        data_bind_native_plan_init(
            plan, &options, &moved, sizeof(moved), &diagnostic) != DATA_BIND_OK)
        return 3;
    if (cmeta_data_buffer_assign(
            &databind_native_text_cmeta_data, &source.label,
            (const unsigned char *)"hello", 5u, 128u) != CMETA_OK ||
        cmeta_data_buffer_assign(
            &databind_native_bytes_cmeta_data, &source.payload,
            bytes, sizeof(bytes), 128u) != CMETA_OK)
        return 4;
    source.count = 88u;
    if (cmeta_data_value_copy(&meta.data, &clone, &source) != CMETA_OK ||
        !clone.label.data || !clone.payload.data ||
        clone.label.data == source.label.data ||
        clone.payload.data == source.payload.data ||
        clone.label.size != 5u || clone.payload.size != sizeof(bytes) ||
        memcmp(clone.label.data, "hello", 5u) != 0 ||
        memcmp(clone.payload.data, bytes, sizeof(bytes)) != 0 ||
        clone.count != 88u)
        return 5;
    source.label.data[0] = 'H';
    source.payload.data[0] = 50u;
    if (memcmp(clone.label.data, "hello", 5u) != 0 ||
        clone.payload.data[0] != 3u)
        return 6;
    if (cmeta_data_value_move(&meta.data, &moved, &clone) != CMETA_OK ||
        moved.count != 88u || moved.label.size != 5u ||
        moved.payload.size != 3u ||
        cmeta_data_value_is_zero(&meta.data, &clone, &zero) != CMETA_OK || !zero)
        return 7;
    if (data_bind_native_plan_clear(
            plan, &options, &moved, sizeof(moved), &diagnostic) != DATA_BIND_OK ||
        data_bind_native_plan_clear(
            plan, &options, &source, sizeof(source), &diagnostic) != DATA_BIND_OK ||
        cmeta_data_value_is_zero(&meta.data, &moved, &zero) != CMETA_OK || !zero)
        return 8;
    data_bind_native_plan_free(plan);
    return 0;
}

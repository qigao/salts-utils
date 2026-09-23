#include <salts/plugin.h>

FunctionDecl(value, int, fixture_plugin_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

int fixture_plugin_add(int left, int right) {
    return left + right;
}

static const salts_plugin_function_export fixture_function_export = {
    .publication = {
        .struct_size = SALTS_PLUGIN_FUNCTION_EXPORT_V1_SIZE,
        .abi_version = SALTS_PLUGIN_PUBLICATION_ABI_VERSION,
        .kind = SALTS_PLUGIN_PUBLICATION_FUNCTION,
        .contract_version = 1u,
        .capabilities = 1u,
        .export_id = "test.math.Add",
        .contract_id = "test.math.Add",
    },
    .function = FunctionMeta(fixture_plugin_add),
    .function_abi = FunctionAbi(fixture_plugin_add),
    .function_entry = (salts_plugin_function_entry)fixture_plugin_add,
};

static const salts_plugin_publication *const fixture_publications[] = {
    &fixture_function_export.publication
};

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_V2_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.loader.function",
    .version = {1u, 0u, 0u},
    .publications = fixture_publications,
    .publication_count = 1u,
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION ? &fixture_manifest : NULL;
}

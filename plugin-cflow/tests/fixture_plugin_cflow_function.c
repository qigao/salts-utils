#include <salts/plugin.h>
#include <salts/thread.h>

FunctionDecl(value, int, plugin_cflow_double,
    (int, value, CMETA_PARAM_IN));

int plugin_cflow_double(int value) {
    return value * 2;
}

static bool SALTS_PLUGIN_CALL plugin_cflow_double_invoke(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    if (context != NULL || return_storage == NULL || params == NULL ||
        param_count != 1u || params[0] == NULL)
        return false;
    *(int *)return_storage = plugin_cflow_double(*(const int *)params[0]);
    return true;
}

static salts_plugin_export fixture_export;
static salts_once_t fixture_once = SALTS_ONCE_INIT;

static void fixture_init(void) {
    fixture_export = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
        .contract_version = 1u,
        .capabilities = 0u,
        .export_id = "test.function.double",
        .contract_id = "test.function",
        .value.function = {
            .desc = FunctionMeta(plugin_cflow_double),
            .abi = FunctionAbi(plugin_cflow_double),
            .context = NULL,
            .invoke = plugin_cflow_double_invoke,
        },
    };
}

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.plugin-cflow.function",
    .version = {1u, 0u, 0u},
    .exports = &fixture_export,
    .export_count = 1u,
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return NULL;
    salts_once(&fixture_once, fixture_init);
    return &fixture_manifest;
}

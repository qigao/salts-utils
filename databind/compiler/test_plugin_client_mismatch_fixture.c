#include <salts/plugin.h>

FunctionDecl(value, int, plugin_client_mismatch_function,
    (int, value, CMETA_PARAM_IN));

int plugin_client_mismatch_function(int value) {
    return value;
}

static bool SALTS_PLUGIN_CALL plugin_client_mismatch_invoke(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    int value;
    int result;
    (void)context;

    if (return_storage == NULL || params == NULL ||
        param_count != 1u || params[0] == NULL)
        return false;

    value = *(const int *)params[0];
    result = plugin_client_mismatch_function(value);
    *(int *)return_storage = result;
    return true;
}

#if defined(PLUGIN_CLIENT_MISMATCH_PLUGIN_ID)
#define TEST_PLUGIN_ID "Image.OtherProcessor"
#define TEST_CONTRACT_ID "Image.Codec"
#elif defined(PLUGIN_CLIENT_MISMATCH_CONTRACT_ID)
#define TEST_PLUGIN_ID "Image.ImageProcessor"
#define TEST_CONTRACT_ID "Image.OtherCodec"
#elif defined(PLUGIN_CLIENT_MISMATCH_FUNCTION)
#define TEST_PLUGIN_ID "Image.ImageProcessor"
#define TEST_CONTRACT_ID "Image.Codec"
#else
#error "one PLUGIN_CLIENT_MISMATCH_* mode is required"
#endif

static const salts_plugin_export mismatch_export = {
    .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
    .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
    .contract_version = 1u,
    .capabilities = 0u,
    .export_id = "Image.Codec.Decode",
    .contract_id = TEST_CONTRACT_ID,
    .value.function = {
        .desc = &plugin_client_mismatch_function__function_meta,
        .abi = &plugin_client_mismatch_function__function_abi_meta,
        .context = NULL,
        .invoke = plugin_client_mismatch_invoke,
    },
};

static const salts_plugin_manifest mismatch_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = TEST_PLUGIN_ID,
    .version = {1u, 0u, 0u},
    .exports = &mismatch_export,
    .export_count = 1u,
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION
               ? &mismatch_manifest
               : NULL;
}

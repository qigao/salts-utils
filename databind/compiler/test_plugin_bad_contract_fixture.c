#include <salts/plugin.h>
#include <salts/thread.h>

#ifndef BAD_CONTRACT_ID
#define BAD_CONTRACT_ID "Image.Codec"
#endif

FunctionDecl(value, int, bad_contract_decode,
    (int, value, CMETA_PARAM_IN));

int bad_contract_decode(int value) {
    return value;
}

static bool SALTS_PLUGIN_CALL bad_contract_invoke(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
  int result;
  (void)context;
  if (return_storage == NULL || params == NULL ||
      param_count != 1u || params[0] == NULL)
    return false;
  result = bad_contract_decode(*(const int *)params[0]);
  *(int *)return_storage = result;
  return true;
}

static salts_plugin_export bad_contract_export = {
    .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
    .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
    .contract_version = 1u,
    .capabilities = 0u,
    .export_id = "Image.Codec.Decode",
    .contract_id = BAD_CONTRACT_ID,
    .value.function = {
        .desc = NULL,
        .abi = NULL,
        .context = NULL,
        .invoke = bad_contract_invoke,
    },
};

static const salts_plugin_manifest bad_contract_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "Image.ImageProcessor",
    .version = {1u, 0u, 0u},
    .exports = &bad_contract_export,
    .export_count = 1u,
};

static salts_once_t bad_contract_once = SALTS_ONCE_INIT;

static void bad_contract_init(void) {
  bad_contract_export.value.function.desc =
      FunctionMeta(bad_contract_decode);
  bad_contract_export.value.function.abi =
      FunctionAbi(bad_contract_decode);
}

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
  if (host_abi != SALTS_PLUGIN_ABI_VERSION)
    return NULL;
  salts_once(&bad_contract_once, bad_contract_init);
  return &bad_contract_manifest;
}

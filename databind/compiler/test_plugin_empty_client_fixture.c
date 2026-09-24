#include <salts/plugin.h>

#ifndef EMPTY_PLUGIN_ID
#define EMPTY_PLUGIN_ID "Image.ImageProcessor"
#endif

static const salts_plugin_manifest empty_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = EMPTY_PLUGIN_ID,
    .version = {1u, 0u, 0u},
    .exports = NULL,
    .export_count = 0u,
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
  return host_abi == SALTS_PLUGIN_ABI_VERSION
             ? &empty_manifest
             : NULL;
}

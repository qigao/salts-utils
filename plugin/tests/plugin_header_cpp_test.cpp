#include <salts/plugin.h>

#include <type_traits>

static_assert(std::is_standard_layout<salts_plugin_version>::value,
              "plugin version must be a C-compatible value");
static_assert(std::is_standard_layout<salts_plugin_export>::value,
              "plugin export must be a C-compatible ABI row");
static_assert(std::is_standard_layout<salts_plugin_manifest>::value,
              "plugin manifest must be a C-compatible ABI row");

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    (void)host_abi;
    return nullptr;
}

int main() {
    salts_plugin_manifest manifest{};
    salts_plugin_query_fn query = &salts_plugin_query;
    const salts_plugin_export *entry = nullptr;

    (void)query;
    return salts_plugin_manifest_find_export(
               &manifest, "missing", &entry) == SALTS_PLUGIN_INVALID_MANIFEST
               ? 0
               : 1;
}

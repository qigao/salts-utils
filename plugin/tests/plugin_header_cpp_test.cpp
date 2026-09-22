#include <salts/plugin.h>

#include <type_traits>

static_assert(std::is_standard_layout<salts_plugin_version>::value,
              "plugin version must be a C-compatible value");
static_assert(std::is_standard_layout<salts_plugin_export>::value,
              "plugin export must be a C-compatible ABI row");
static_assert(std::is_standard_layout<salts_plugin_manifest>::value,
              "plugin manifest must be a C-compatible ABI row");

int main() {
    salts_plugin_manifest manifest{};
    salts_plugin_query_fn query = nullptr;
    const salts_plugin_export *entry = nullptr;

    (void)query;
    return salts_plugin_manifest_find_export(
               &manifest, "missing", &entry) == SALTS_PLUGIN_INVALID_MANIFEST
               ? 0
               : 1;
}

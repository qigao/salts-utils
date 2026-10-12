#include "calc_component.h"
#include <salts/component.h>
#include <cstring>
#include <type_traits>

static_assert(std::is_standard_layout<
                  sidlc_11_WasmRuntime_10_Calculator_4_Calc>::value,
              "Generated Interface must use the C-compatible native carrier");
static_assert(std::is_standard_layout<
                  salts_component_provider_binding>::value,
              "Provider binding must retain the existing Salts ABI");

int main() {
    if (!cmeta_component_desc_valid(
            &sidlc_11_WasmRuntime_10_Calculator__component_meta))
        return 1;
    return std::strcmp(
        sidlc_11_WasmRuntime_10_Calculator__component_meta.stable_id,
        "WasmRuntime.Calculator") == 0 ? 0 : 2;
}

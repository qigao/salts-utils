#include "calc_component_plugin.h"
#include <salts/component.h>
#include <cstring>
#include <type_traits>

static_assert(std::is_standard_layout<
                  sidlc_11_WasmRuntime_10_Calculator_4_Calc>::value,
              "Generated Interface must use the C-compatible native carrier");
static_assert(std::is_standard_layout<
                  salts_component_provider_binding>::value,
              "Provider binding must retain the existing Salts ABI");
/* The native records are deliberately opaque here: their owning init/clear
 * implementation remains the sole canonical C11 TYPES output. */
using calc_native_add_fn = int (*)(void *, const AddRequest *, AddResponse *);
static_assert(std::is_same<
                  decltype(((sidlc_11_WasmRuntime_10_Calculator_4_Calc_vtable *)nullptr)->Add),
                  calc_native_add_fn>::value,
              "Generated C++17 interface must retain exact native C pointer types");

int main() {
    if (!cmeta_component_desc_valid(
            &sidlc_11_WasmRuntime_10_Calculator__component_meta))
        return 1;
    return std::strcmp(
        sidlc_11_WasmRuntime_10_Calculator__component_meta.stable_id,
        "WasmRuntime.Calculator") == 0 ? 0 : 2;
}

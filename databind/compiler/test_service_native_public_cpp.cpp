#include "service_native_generated.h"

#include <type_traits>

using add_fn = int (*)(
    const AddRequest_t *request,
    AddResponse_t *response);

static_assert(
    std::is_same_v<decltype(&databind_13_ServiceNative_4_Calc_3_Add), add_fn>,
    "generated Service declaration must preserve the canonical native C ABI");

int main() {
    return 0;
}

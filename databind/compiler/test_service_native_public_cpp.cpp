#include "service_native_generated.h"

#include <type_traits>

using add_fn = int (*)(
    const AddRequest_t *request,
    AddResponse_t *response);

using find_error =
    databind_13_ServiceNative_4_Calc_4_Find__error;
using find_fn = int (*)(
    const AddRequest_t *request,
    AddResponse_t *response,
    find_error *error);

static_assert(
    std::is_same_v<decltype(&databind_13_ServiceNative_4_Calc_3_Add), add_fn>,
    "generated Service declaration must preserve the canonical native C ABI");
static_assert(
    std::is_standard_layout_v<find_error>,
    "generated typed-error envelope must remain a C-compatible layout");
static_assert(
    std::is_same_v<decltype(&databind_13_ServiceNative_4_Calc_4_Find), find_fn>,
    "typed-error Service declaration must preserve request/response/error ABI");

int main() {
    return 0;
}

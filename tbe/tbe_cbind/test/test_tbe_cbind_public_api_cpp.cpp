#include <tbe_cbind/tbe_cbind.h>

#include "tinytest.h"

spec("TbeCBind public C++ API") {
  it("links and calls every public function through the C ABI") {
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = nullptr;

    tbe_cbind_plan_options_init(&options);
    tbe_cbind_plan_error_init(&error);
    check_true(tbe_cbind_plan_create_from_text(
                   nullptr, 0u, nullptr, 0u, nullptr, &options, &plan, &error) ==
               TBE_CBIND_INVALID_ARGUMENT);
    check_null(plan);
    check_null(tbe_cbind_plan_shape(plan));
    check_true(tbe_cbind_plan_decode(plan, nullptr, nullptr, nullptr, nullptr) ==
               CBIND_INVALID_ARGUMENT);
    tbe_cbind_plan_destroy(plan);
  }
}

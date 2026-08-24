#include "cbind_sidecar.h"

#include "tinytest.hpp"

spec("generated CBind sidecar C++ linkage") {
  it("exposes its descriptor accessor through the generated public header") {
    const cmeta_data_desc *descriptor = CBindEnvelope_cbind_data();

    check_not_null(descriptor);
    check_true(cmeta_data_desc_valid(descriptor));
  }
}

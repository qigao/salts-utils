#include "data_bind_cflow.h"
#include "tinytest.hpp"

#include <type_traits>

static_assert(std::is_standard_layout<DataBindValueRef>::value,
              "DataBindValueRef must remain a C-compatible value");
static_assert(std::is_standard_layout<DataBindFieldRef>::value,
              "DataBindFieldRef must remain a C-compatible value");
static_assert(std::is_standard_layout<DataBindMapEntryRef>::value,
              "DataBindMapEntryRef must remain a C-compatible value");
static_assert(DATA_BIND_CMETA_RANGE_VALUES == 1, "range kind values are ABI-visible");
static_assert(DATA_BIND_CMETA_RANGE_FIELDS == 2, "range kind values are ABI-visible");
static_assert(DATA_BIND_CMETA_RANGE_MAP_ENTRIES == 3, "range kind values are ABI-visible");

spec("data_bind adapter C++ headers") {
  it("should expose zero-initializable C handles") {
    cmeta_range range{};
    cflow_stream stream{};
    cflow_publisher publisher{};

    check_null(range.object);
    check(sizeof(stream) > 0u);
    check_null(publisher.self);
  }
}

#include "data_bind_cflow.h"
#include "data_bind_json_provider.h"
#include "data_bind_yaml_provider.h"
#include "data_bind_csv_provider.h"
#include "data_bind_xml_provider.h"
#include "data_bind_temporal_adapter.h"
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
  it("links adapter entry points from the unified C runtime") {
    check_not_null(data_bind_cmeta_value_ref_type());
    check_not_null(data_bind_cmeta_field_ref_type());
    check_not_null(data_bind_cmeta_map_entry_ref_type());
    check_equal(data_bind_json_format_provider()->format, DATA_BIND_FORMAT_JSON);
    check_equal(data_bind_yaml_format_provider()->format, DATA_BIND_FORMAT_YAML);
    check_equal(data_bind_csv_format_provider()->format, DATA_BIND_FORMAT_CSV);
    check_equal(data_bind_xml_format_provider()->format, DATA_BIND_FORMAT_XML);

    DataBindDate date{};
    static const char text[] = "2026-10-07";
    check_equal(data_bind_temporal_parse_date(text, sizeof(text) - 1, &date), DATA_BIND_OK);
    check_equal(date.year, 2026);

    cflow_stream stream{};
    cflow_publisher publisher{};
    check_equal(data_bind_cflow_stream_from_value(
                    nullptr, DATA_BIND_CMETA_RANGE_VALUES, &stream),
                DATA_BIND_ERR_INVALID_ARG);
    check_equal(data_bind_cflow_publisher_from_value(
                    nullptr, DATA_BIND_CMETA_RANGE_VALUES, &publisher),
                DATA_BIND_ERR_INVALID_ARG);
    cflow_stream_destroy(&stream);
    cflow_publisher_destroy(&publisher);
  }

  it("should expose zero-initializable C handles") {
    cmeta_range range{};
    cflow_stream stream{};
    cflow_publisher publisher{};

    check_null(range.object);
    check(sizeof(stream) > 0u);
    check_null(publisher.self);
  }
}

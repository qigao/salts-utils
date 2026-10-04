#include "data_bind_format_provider.h"

#include "data_bind_csv_provider.h"
#include "data_bind_json_provider.h"
#include "data_bind_xml_provider.h"
#include "data_bind_yaml_provider.h"

DATA_BIND_API const DataBindFormatProvider *
data_bind_builtin_format_provider(DataBindFormat format) {
  switch (format) {
  case DATA_BIND_FORMAT_JSON:
    return data_bind_json_format_provider();
  case DATA_BIND_FORMAT_YAML:
    return data_bind_yaml_format_provider();
  case DATA_BIND_FORMAT_CSV:
    return data_bind_csv_format_provider();
  case DATA_BIND_FORMAT_XML:
    return data_bind_xml_format_provider();
  case DATA_BIND_FORMAT_BINARY:
  case DATA_BIND_FORMAT_NONE:
  default:
    return NULL;
  }
}

DATA_BIND_API DataBindStatus data_bind_builtin_format_reader_open_csv_row(
    const char *data,
    size_t len,
    size_t row,
    size_t max_depth,
    DataBindFormatReader *out_reader,
    DataBindError *error) {
  return data_bind_csv_format_reader_open_row(
      data, len, row, max_depth, out_reader, error);
}

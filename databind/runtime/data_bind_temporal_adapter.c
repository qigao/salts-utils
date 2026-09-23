#include "data_bind_temporal_adapter.h"

#include <datetime_parser.h>

#include <limits.h>
#include <time.h>

static DataBindDateTime temporal_from_native(datetime_t value) {
  return (DataBindDateTime){
      value.year, value.month, value.day, value.hour, value.minute,
      value.second, value.millisecond, value.tz_offset, value.has_tz,
      value.day_of_week};
}

static datetime_t temporal_to_native(DataBindDateTime value) {
  return (datetime_t){
      value.year, value.month, value.day, value.hour, value.minute,
      value.second, value.millisecond, value.tz_offset, value.has_tz,
      value.day_of_week};
}

DataBindStatus data_bind_temporal_parse_datetime(
    const char *text, size_t len, DataBindDateTime *out) {
  datetime_t native;
  if (text == NULL || out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (datetime_parse(text, len, &native) != 0)
    return DATA_BIND_ERR_PARSE;
  *out = temporal_from_native(native);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_temporal_parse_date(
    const char *text, size_t len, DataBindDate *out) {
  DataBindDateTime value;
  DataBindStatus status;
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_temporal_parse_datetime(text, len, &value);
  if (status != DATA_BIND_OK) return status;
  if (value.year <= 0 || value.month <= 0 || value.day <= 0)
    return DATA_BIND_ERR_PARSE;
  *out = (DataBindDate){value.year, value.month, value.day};
  return DATA_BIND_OK;
}

DataBindStatus data_bind_temporal_parse_time(
    const char *text, size_t len, DataBindTime *out) {
  DataBindDateTime value;
  DataBindStatus status;
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_temporal_parse_datetime(text, len, &value);
  if (status != DATA_BIND_OK) return status;
  if (value.year != 0 || value.month != 0 || value.day != 0 ||
      value.has_tz || value.hour < 0 || value.hour > 23 ||
      value.minute < 0 || value.minute > 59 ||
      value.second < 0 || value.second > 60 ||
      value.millisecond < 0 || value.millisecond > 999)
    return DATA_BIND_ERR_PARSE;
  *out = (DataBindTime){
      value.hour, value.minute, value.second, value.millisecond};
  return DATA_BIND_OK;
}

DataBindStatus data_bind_temporal_to_unix_seconds(
    const DataBindDateTime *value, int64_t *out_seconds) {
  datetime_t native;
  time_t timestamp;
  if (value == NULL || out_seconds == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  native = temporal_to_native(*value);
  timestamp = datetime_to_time(&native);
  if (timestamp == (time_t)-1) return DATA_BIND_ERR_PARSE;
  if (sizeof(time_t) > sizeof(int64_t) &&
      (timestamp > (time_t)INT64_MAX || timestamp < (time_t)INT64_MIN))
    return DATA_BIND_ERR_LIMIT;
  *out_seconds = (int64_t)timestamp;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_temporal_format_rfc822(
    const DataBindDateTime *value, char *out, size_t out_size) {
  int64_t seconds;
  time_t timestamp;
  DataBindStatus status;
  if (value == NULL || out == NULL || out_size == 0u)
    return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_temporal_to_unix_seconds(value, &seconds);
  if (status != DATA_BIND_OK) return status;
  timestamp = (time_t)seconds;
  if ((int64_t)timestamp != seconds) return DATA_BIND_ERR_LIMIT;
  return datetime_format_rfc822(timestamp, out, out_size) >= 0
      ? DATA_BIND_OK
      : DATA_BIND_ERR_BUFFER_TOO_SMALL;
}

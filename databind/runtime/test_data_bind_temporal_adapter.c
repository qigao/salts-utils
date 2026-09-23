#include "data_bind_temporal_adapter.h"

#include <string.h>

int main(void) {
  static const char rfc[] = "Sat, 04 Mar 2006 13:27:54 GMT";
  static const char date_text[] = "2026-06-28";
  static const char time_text[] = "09:30:05.123";
  DataBindDateTime datetime = {0};
  DataBindDate date = {0};
  DataBindTime time = {0};
  int64_t seconds = 0;
  char output[64];

  if (data_bind_temporal_parse_datetime(
          rfc, sizeof(rfc) - 1u, &datetime) != DATA_BIND_OK)
    return 1;
  if (datetime.year != 2006 || datetime.month != 3 || datetime.day != 4)
    return 2;
  if (data_bind_temporal_to_unix_seconds(&datetime, &seconds) != DATA_BIND_OK ||
      seconds != INT64_C(1141478874))
    return 3;
  if (data_bind_temporal_format_rfc822(
          &datetime, output, sizeof(output)) != DATA_BIND_OK)
    return 4;
  if (strstr(output, "04 Mar 2006 13:27:54 GMT") == NULL)
    return 5;

  if (data_bind_temporal_parse_date(
          date_text, sizeof(date_text) - 1u, &date) != DATA_BIND_OK)
    return 6;
  if (date.year != 2026 || date.month != 6 || date.day != 28)
    return 7;

  if (data_bind_temporal_parse_time(
          time_text, sizeof(time_text) - 1u, &time) != DATA_BIND_OK)
    return 8;
  if (time.hour != 9 || time.minute != 30 ||
      time.second != 5 || time.millisecond != 123)
    return 9;

  return 0;
}

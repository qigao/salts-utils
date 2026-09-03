#ifndef TURBO_PARSER_DATETIME_H
#define TURBO_PARSER_DATETIME_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Datetime Parser */
typedef struct {
  int year;        // e.g., 2006
  int month;       // 1-12
  int day;         // 1-31
  int hour;        // 0-23
  int minute;      // 0-59
  int second;      // 0-59
  int millisecond; // 0-999
  int tz_offset;   // in minutes from UTC (e.g., +03:45 -> 225)
  int has_tz;      // bool
  int day_of_week; // 0-6 (Sun-Sat), -1 if not set
} turbo_datetime_t;

/**
 * @brief Parses a date-time string in various formats (RFC-822, ISO-8601, HTTP, NCSA, etc.).
 * Supported formats:
 * - Compact: YYYYMMDD, HHMMSS, etc.
 * - ISO-8601: 2006-03-14T13:27:54+03:45
 * - RFC-822 / HTTP: Sat, 04 Mar 2006 13:27:54 GMT
 * - NCSA: 04/Mar/2006:13:27:54 -0500
 * - Various common slash, hyphen, and dot separated formats.
 *
 * @param str The date string to parse.
 * @param len Length of the string.
 * @param out Output datetime structure.
 * @return 0 on success, -1 on failure.
 */
TURBO_PARSER_API int turbo_parse_datetime(const char *str, size_t len, turbo_datetime_t *out);

/**
 * @brief Converts turbo_datetime_t to time_t (UTC).
 *
 * @param dt Input datetime structure.
 * @return time_t value (seconds since epoch), or -1 on error.
 */
TURBO_PARSER_API time_t turbo_datetime_to_time(const turbo_datetime_t *dt);

/**
 * @brief Formats a time_t as an RFC 7231 / RFC 822 HTTP date-time string.
 *
 * @param t Time to format.
 * @param buf Output buffer (at least 30 bytes).
 * @param buf_len Size of the buffer.
 * @return Number of characters written, or -1 on failure.
 */
TURBO_PARSER_API int turbo_datetime_format_rfc822(time_t t, char *buf, size_t buf_len);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_DATETIME_H

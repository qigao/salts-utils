#if defined(__linux__) && !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include "jinja_float.h"

#include <salts/thread.h>

#include <locale.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static salts_once_t jinja_float_locale_once = SALTS_ONCE_INIT;
#if defined(_WIN32)
static _locale_t jinja_float_c_locale;
#else
static locale_t jinja_float_c_locale;
#endif

static void jinja_float_locale_init(void) {
  /* Process-lifetime locale keeps compile and render paths allocation-free after first use. */
#if defined(_WIN32)
  jinja_float_c_locale = _create_locale(LC_NUMERIC, "C");
#else
  jinja_float_c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
#endif
}

static int jinja_float_locale_ready(void) {
  salts_once(&jinja_float_locale_once, jinja_float_locale_init);
  return jinja_float_c_locale != NULL;
}

static double jinja_float_strtod(const char *text, char **end) {
#if defined(_WIN32)
  return _strtod_l(text, end, jinja_float_c_locale);
#else
  locale_t previous = uselocale(jinja_float_c_locale);
  double value;
  if (previous == (locale_t)0) {
    if (end != NULL) *end = (char *)text;
    return 0.0;
  }
  value = strtod(text, end);
  if (uselocale(previous) == (locale_t)0) {
    if (end != NULL) *end = (char *)text;
    return 0.0;
  }
  return value;
#endif
}

static int jinja_float_snprintf(char *buffer, size_t capacity, const char *format,
                                int precision, double value) {
#if defined(_WIN32)
  if (buffer == NULL) return _scprintf_l(format, jinja_float_c_locale, precision, value);
  return _snprintf_s_l(buffer, capacity, _TRUNCATE, format, jinja_float_c_locale, precision, value);
#else
  locale_t previous = uselocale(jinja_float_c_locale);
  int written;
  if (previous == (locale_t)0) return -1;
  written = snprintf(buffer, capacity, format, precision, value);
  if (uselocale(previous) == (locale_t)0) return -1;
  return written;
#endif
}

static int jinja_float_same(double left, double right) {
  return left == right && (left != 0.0 || signbit(left) == signbit(right));
}

int jinja_float_parse(const char *text, size_t size, double *value) {
  char cleaned[JINJA_FLOAT_TOKEN_CAPACITY];
  char *end = NULL;
  size_t input;
  size_t output = 0u;
  double parsed;

  if (text == NULL || value == NULL || size == 0u || size >= sizeof(cleaned) ||
      !jinja_float_locale_ready())
    return 0;
  for (input = 0u; input < size; ++input) {
    if (text[input] == '_') continue;
    cleaned[output++] = text[input];
  }
  if (output == 0u) return 0;
  cleaned[output] = '\0';
  parsed = jinja_float_strtod(cleaned, &end);
  if (end != cleaned + output) return 0;
  *value = parsed;
  return 1;
}

int jinja_float_parse_text(char *text, size_t size, double *value) {
  size_t start = size != 0u && (text[0] == '+' || text[0] == '-') ? 1u : 0u;
  const char *word = text + start;
  int digits = 0, point = 0, exponent = 0;
  if (strcmp(word, "inf") != 0 && strcmp(word, "infinity") != 0 && strcmp(word, "nan") != 0) {
    for (size_t i = start; i < size; ++i) {
      char c = text[i];
      if (c >= '0' && c <= '9') digits = 1;
      else if (c == '_') {
        if (i == start || text[i - 1u] < '0' || text[i - 1u] > '9' ||
            i + 1u == size || text[i + 1u] < '0' || text[i + 1u] > '9') return 0;
      } else if (c == '.' && !point && !exponent) point = 1;
      else if (c == 'e' && !exponent && digits) {
        exponent = 1;
        digits = 0;
        if (i + 1u < size && (text[i + 1u] == '+' || text[i + 1u] == '-')) ++i;
      } else return 0;
    }
    if (!digits) return 0;
  }
  if (!jinja_float_locale_ready()) return 0;
  size_t length = 0u;
  for (size_t i = 0u; i < size; ++i)
    if (text[i] != '_') text[length++] = text[i];
  text[length] = '\0';
  char *end;
  *value = jinja_float_strtod(text, &end);
  return end == text + length;
}

/* Decimal formatting rounds the original binary64 value, avoiding a second
 * rounding from value * 10^precision. For negative precision, values >= 1 have
 * at most DBL_MANT_DIG fractional decimal places; retain them for exact ties.
 * Storage/work are bounded by the binary64 exponent and significand ranges. */
int jinja_float_round(double value, int64_t precision, double *result) {
  enum { MAX_DECIMAL_PRECISION = 323, MIN_DECIMAL_PRECISION = -308 };
  char text[JINJA_FLOAT_TOKEN_CAPACITY];
  *result = value;
  if (!isfinite(value) || value == 0.0 || precision > MAX_DECIMAL_PRECISION) return 1;
  if (precision < MIN_DECIMAL_PRECISION || (precision < 0 && fabs(value) < 1.0)) {
    *result = copysign(0.0, value);
    return 1;
  }
  if (!jinja_float_locale_ready()) return 0;
  int written = jinja_float_snprintf(text, sizeof(text), "%.*f",
      precision < 0 ? DBL_MANT_DIG : (int)precision, fabs(value));
  if (written < 0 || (size_t)written >= sizeof(text)) return 0;
  if (precision < 0) {
    size_t whole = (size_t)(strchr(text, '.') - text);
    int64_t cut = (int64_t)whole + precision;
    if (cut < 0) {
      *result = copysign(0.0, value);
      return 1;
    }
    int upward = text[cut] > '5';
    if (text[cut] == '5') {
      upward = cut != 0 && (text[cut - 1] - '0') % 2 != 0;
      for (size_t i = (size_t)cut + 1u; i < (size_t)written; ++i)
        if (text[i] >= '1' && text[i] <= '9') { upward = 1; break; }
    }
    size_t carry = (size_t)cut;
    while (upward && carry != 0u) {
      --carry;
      upward = text[carry] == '9';
      text[carry] = upward ? '0' : (char)(text[carry] + 1);
    }
    if (upward) {
      text[0] = '1';
      ++whole;
      cut = 1;
    }
    memset(text + (size_t)cut, '0', whole - (size_t)cut);
    text[whole] = '\0';
  }
  *result = copysign(jinja_float_strtod(text, NULL), value);
  return isfinite(*result);
}

int jinja_float_format_fixed(double value, int precision, char *buffer, size_t capacity, size_t *size) {
  if (!isfinite(value)) return jinja_float_format(value, buffer, capacity, size);
  if (!jinja_float_locale_ready()) return 0;
  int written = jinja_float_snprintf(buffer, capacity, "%.*f", precision, value);
  if (written < 0 || (size_t)written >= capacity) return 0;
  *size = (size_t)written;
  return 1;
}

int jinja_float_format_spec(double value, char conversion, int alternate, int precision,
    char *buffer, size_t capacity) {
  if (!jinja_float_locale_ready()) return -1;
  char format[] = "%#.*f";
  format[sizeof(format) - 2u] = conversion;
  const char *spec = format;
  if (!alternate) { format[1] = '%'; spec = format + 1u; }
  if (isnan(value)) value = NAN;
  return jinja_float_snprintf(buffer, capacity, spec, precision, value);
}

static int jinja_float_append(char *buffer, size_t capacity, size_t *position, const char *text,
                              size_t size) {
  if (*position > capacity || size > capacity - *position) return 0;
  if (size != 0u) memcpy(buffer + *position, text, size);
  *position += size;
  return 1;
}

static int jinja_float_append_char(char *buffer, size_t capacity, size_t *position, char value) {
  return jinja_float_append(buffer, capacity, position, &value, 1u);
}

static int jinja_float_normalize_candidate(const char *candidate, char *digits, size_t *digit_count,
                                           int *decimal_exponent, int *negative) {
  const char *cursor = candidate;
  size_t count = 0u;
  size_t digits_before_point = 0u;
  size_t first_nonzero = SIZE_MAX;
  int exponent = 0;
  int exponent_negative = 0;
  int saw_point = 0;

  *negative = *cursor == '-';
  if (*cursor == '-' || *cursor == '+') ++cursor;
  while (*cursor != '\0' && *cursor != 'e' && *cursor != 'E') {
    if (*cursor == '.') {
      if (saw_point) return 0;
      saw_point = 1;
    } else if (*cursor >= '0' && *cursor <= '9') {
      if (count == JINJA_FLOAT_TEXT_CAPACITY) return 0;
      if (!saw_point) ++digits_before_point;
      if (*cursor != '0' && first_nonzero == SIZE_MAX) first_nonzero = count;
      digits[count++] = *cursor;
    } else {
      return 0;
    }
    ++cursor;
  }
  if (*cursor == 'e' || *cursor == 'E') {
    ++cursor;
    if (*cursor == '+' || *cursor == '-') {
      exponent_negative = *cursor == '-';
      ++cursor;
    }
    if (*cursor == '\0') return 0;
    while (*cursor != '\0') {
      if (*cursor < '0' || *cursor > '9' || exponent > 1000) return 0;
      exponent = exponent * 10 + (*cursor - '0');
      ++cursor;
    }
    if (exponent_negative) exponent = -exponent;
  }
  if (first_nonzero == SIZE_MAX) return 0;
  *decimal_exponent = exponent + (int)digits_before_point - (int)first_nonzero - 1;
  memmove(digits, digits + first_nonzero, count - first_nonzero);
  count -= first_nonzero;
  while (count > 1u && digits[count - 1u] == '0')
    --count;
  *digit_count = count;
  return 1;
}

int jinja_float_format(double value, char *buffer, size_t capacity, size_t *size) {
  char candidate[JINJA_FLOAT_TEXT_CAPACITY];
  char digits[JINJA_FLOAT_TEXT_CAPACITY];
  size_t digit_count = 0u;
  size_t position = 0u;
  int decimal_exponent = 0;
  int negative = 0;
  int precision;

  if (buffer == NULL || size == NULL || capacity == 0u || !jinja_float_locale_ready()) return 0;
  if (isnan(value)) {
    if (!jinja_float_append(buffer, capacity, &position, "nan", 3u)) return 0;
    *size = position;
    return 1;
  }
  if (isinf(value)) {
    const char *text = signbit(value) ? "-inf" : "inf";
    size_t length = signbit(value) ? 4u : 3u;
    if (!jinja_float_append(buffer, capacity, &position, text, length)) return 0;
    *size = position;
    return 1;
  }
  if (value == 0.0) {
    const char *text = signbit(value) ? "-0.0" : "0.0";
    size_t length = signbit(value) ? 4u : 3u;
    if (!jinja_float_append(buffer, capacity, &position, text, length)) return 0;
    *size = position;
    return 1;
  }

  for (precision = 1; precision <= 17; ++precision) {
    char *end = NULL;
    double round_trip;
    int written = jinja_float_snprintf(candidate, sizeof(candidate), "%.*g", precision, value);
    if (written <= 0 || (size_t)written >= sizeof(candidate)) return 0;
    round_trip = jinja_float_strtod(candidate, &end);
    if (*end == '\0' && jinja_float_same(value, round_trip)) break;
  }
  if (precision > 17 || !jinja_float_normalize_candidate(candidate, digits, &digit_count,
                                                         &decimal_exponent, &negative))
    return 0;
  if (negative && !jinja_float_append_char(buffer, capacity, &position, '-')) return 0;

  if (decimal_exponent >= -4 && decimal_exponent < 16) {
    if (decimal_exponent < 0) {
      int zeros = -decimal_exponent - 1;
      if (!jinja_float_append(buffer, capacity, &position, "0.", 2u)) return 0;
      while (zeros-- > 0)
        if (!jinja_float_append_char(buffer, capacity, &position, '0')) return 0;
      if (!jinja_float_append(buffer, capacity, &position, digits, digit_count)) return 0;
    } else {
      size_t whole_digits = (size_t)decimal_exponent + 1u;
      if (digit_count <= whole_digits) {
        size_t zeros = whole_digits - digit_count;
        if (!jinja_float_append(buffer, capacity, &position, digits, digit_count)) return 0;
        while (zeros-- > 0u)
          if (!jinja_float_append_char(buffer, capacity, &position, '0')) return 0;
        if (!jinja_float_append(buffer, capacity, &position, ".0", 2u)) return 0;
      } else {
        if (!jinja_float_append(buffer, capacity, &position, digits, whole_digits) ||
            !jinja_float_append_char(buffer, capacity, &position, '.') ||
            !jinja_float_append(buffer, capacity, &position, digits + whole_digits,
                                digit_count - whole_digits))
          return 0;
      }
    }
  } else {
    int exponent_magnitude = decimal_exponent < 0 ? -decimal_exponent : decimal_exponent;
    char exponent_digits[8];
    int exponent_length;
    if (!jinja_float_append_char(buffer, capacity, &position, digits[0])) return 0;
    if (digit_count > 1u &&
        (!jinja_float_append_char(buffer, capacity, &position, '.') ||
         !jinja_float_append(buffer, capacity, &position, digits + 1u, digit_count - 1u)))
      return 0;
    if (!jinja_float_append_char(buffer, capacity, &position, 'e') ||
        !jinja_float_append_char(buffer, capacity, &position, decimal_exponent < 0 ? '-' : '+'))
      return 0;
    exponent_length =
        snprintf(exponent_digits, sizeof(exponent_digits), "%02d", exponent_magnitude);
    if (exponent_length < 2 || (size_t)exponent_length >= sizeof(exponent_digits) ||
        !jinja_float_append(buffer, capacity, &position, exponent_digits, (size_t)exponent_length))
      return 0;
  }
  *size = position;
  return 1;
}

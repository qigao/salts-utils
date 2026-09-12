#ifndef JINJA_FLOAT_H
#define JINJA_FLOAT_H

#include <stddef.h>
#include <stdint.h>

#define JINJA_FLOAT_TOKEN_CAPACITY 768u
#define JINJA_FLOAT_TEXT_CAPACITY 64u

int jinja_float_parse(const char *text, size_t size, double *value);
/* Normalized ASCII, NUL-terminated; removes valid digit separators in place. */
int jinja_float_parse_text(char *text, size_t size, double *value);
int jinja_float_round(double value, int64_t precision, double *result);
int jinja_float_format(double value, char *buffer, size_t capacity, size_t *size);
int jinja_float_format_fixed(double value, int precision, char *buffer, size_t capacity, size_t *size);
/* Private printf conversions e/E/f/F/g/G; NULL buffer measures required bytes. */
int jinja_float_format_spec(double value, char conversion, int alternate, int precision,
    char *buffer, size_t capacity);

#endif

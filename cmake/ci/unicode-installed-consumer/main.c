#include <salts_unicode.h>
#include <jinja/jinja_cmeta.h>
#include <string.h>
int main(void) {
  const char bytes[] = "A\xc3\xa9";
  vstr input = vstr_from_buf(bytes, sizeof(bytes) - 1u);
  salts_unicode_scalar scalar = {0};
  size_t cursor = 0u;
  if (strcmp(salts_unicode_version(), "17.0.0") != 0) return 1;
  if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK) return 2;
  if (scalar.value != 0x41u || cursor != 1u) return 3;
  if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK) return 4;
  if (scalar.value != 0xe9u || cursor != sizeof(bytes) - 1u) return 5;
  return 0;
}

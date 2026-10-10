#ifndef SALTS_CRYPTO_TEST_HEX_H
#define SALTS_CRYPTO_TEST_HEX_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
static size_t unhex(const char *text, uint8_t *out) {
  static const char digits[] = "0123456789abcdef";
  size_t i, n = strlen(text) / 2;
  if (strlen(text) % 2) abort();
  for (i = 0; i < n; ++i) {
    out[i] = (uint8_t)(((strchr(digits, text[2*i]) - digits) << 4) |
                       (strchr(digits, text[2*i+1]) - digits));
  }
  return n;
}

#endif

#include "search_literal.h"

#include <stdlib.h>
#include <string.h>

/* Maximal suffix in either lexicographic direction (critical factorization).
 * The suffix start is stored as plus-one to avoid signed -1 sentinels.
 * At entry, ms_plus_one==0 is equivalent to the theoretical suffix -1.
 * The invariant j+k<length makes every needle access in-bounds. */
static size_t maximal_suffix(const unsigned char *needle, size_t length,
                             bool reversed_order, size_t *out_period) {
  size_t ms_plus_one = 0;
  size_t j = 0;
  size_t k = 1;
  size_t period = 1;

  while (j < length && k < length - j) {
    const unsigned char a = needle[j + k];
    const unsigned char b = needle[ms_plus_one + k - 1];
    if ((!reversed_order && a < b) || (reversed_order && a > b)) {
      j += k;
      k = 1;
      period = j + 1 - ms_plus_one;
    } else if (a == b) {
      if (k < period) {
        ++k;
      } else {
        j += period;
        k = 1;
      }
    } else {
      ms_plus_one = j + 1;
      j = ms_plus_one;
      k = 1;
      period = 1;
    }
  }
  *out_period = period;
  return ms_plus_one;
}

salts_search_literal_status salts_search_literal_compile(
    salts_search_literal_pattern *out, const void *needle,
    size_t needle_length, size_t max_pattern_bytes) {
  size_t cut_a, cut_b, period_a, period_b;
  unsigned char *owned = NULL;
  if (out == NULL || out->compiled || out->bytes != NULL ||
      out->length != 0 || max_pattern_bytes == 0 ||
      (needle == NULL && needle_length != 0))
    return SALTS_SEARCH_LITERAL_INVALID;
  if (needle_length > max_pattern_bytes)
    return SALTS_SEARCH_LITERAL_LIMIT;

  if (needle_length != 0) {
    owned = (unsigned char *)malloc(needle_length);
    if (owned == NULL)
      return SALTS_SEARCH_LITERAL_NO_MEMORY;
    memcpy(owned, needle, needle_length);
    cut_a = maximal_suffix(owned, needle_length, false, &period_a);
    cut_b = maximal_suffix(owned, needle_length, true, &period_b);
    if (cut_a > cut_b) {
      out->critical_position = cut_a;
      out->period = period_a;
    } else {
      out->critical_position = cut_b;
      out->period = period_b;
    }
    /* A periodic prefix must equal the bytes shifted by its minimal period.
     * This comparison is guarded against overflow and out-of-bounds. */
    out->periodic =
        out->period < needle_length &&
        out->critical_position <= needle_length - out->period &&
        memcmp(owned, owned + out->period, out->critical_position) == 0;
    if (!out->periodic) {
      const size_t left = out->critical_position;
      const size_t right = needle_length - left;
      out->period = (left > right ? left : right) + 1;
    }
  }

  out->bytes = owned;
  out->length = needle_length;
  out->compiled = true;
  return SALTS_SEARCH_LITERAL_OK;
}

salts_search_literal_status salts_search_literal_find(
    const salts_search_literal_pattern *pattern, const void *text,
    size_t text_length, size_t start_at, salts_search_literal_span *out_span) {
  const unsigned char *haystack = (const unsigned char *)text;
  const unsigned char *needle;
  size_t j, available, limit;
  if (out_span != NULL) {
    out_span->start = 0;
    out_span->end = 0;
  }
  if (pattern == NULL || !pattern->compiled || out_span == NULL ||
      (text_length != 0 && text == NULL) ||
      (pattern->length != 0 && pattern->bytes == NULL))
    return SALTS_SEARCH_LITERAL_INVALID;
  if (start_at > text_length)
    return SALTS_SEARCH_LITERAL_NOT_FOUND;
  if (pattern->length == 0) {
    out_span->start = start_at;
    out_span->end = start_at;
    return SALTS_SEARCH_LITERAL_OK;
  }
  available = text_length - start_at;
  if (available < pattern->length)
    return SALTS_SEARCH_LITERAL_NOT_FOUND;
  needle = pattern->bytes;
  if (pattern->length == 1) {
    const unsigned char *hit =
        (const unsigned char *)memchr(haystack + start_at, needle[0], available);
    if (hit == NULL)
      return SALTS_SEARCH_LITERAL_NOT_FOUND;
    out_span->start = (size_t)(hit - haystack);
    out_span->end = out_span->start + 1;
    return SALTS_SEARCH_LITERAL_OK;
  }

  limit = text_length - pattern->length;
  j = start_at;
  if (pattern->periodic) {
    size_t memory = 0;
    while (j <= limit) {
      size_t i = pattern->critical_position > memory
                     ? pattern->critical_position : memory;
      while (i < pattern->length && needle[i] == haystack[j + i])
        ++i;
      if (i == pattern->length) {
        i = pattern->critical_position;
        while (i > memory && needle[i - 1] == haystack[j + i - 1])
          --i;
        if (i <= memory) {
          out_span->start = j;
          out_span->end = j + pattern->length;
          return SALTS_SEARCH_LITERAL_OK;
        }
        if (pattern->period > limit - j)
          break;
        j += pattern->period;
        memory = pattern->length - pattern->period;
      } else {
        const size_t shift = i - pattern->critical_position + 1;
        if (shift > limit - j)
          break;
        j += shift;
        memory = 0;
      }
    }
  } else {
    while (j <= limit) {
      size_t i = pattern->critical_position;
      while (i < pattern->length && needle[i] == haystack[j + i])
        ++i;
      if (i == pattern->length) {
        i = pattern->critical_position;
        while (i > 0 && needle[i - 1] == haystack[j + i - 1])
          --i;
        if (i == 0) {
          out_span->start = j;
          out_span->end = j + pattern->length;
          return SALTS_SEARCH_LITERAL_OK;
        }
        if (pattern->period > limit - j)
          break;
        j += pattern->period;
      } else {
        const size_t shift = i - pattern->critical_position + 1;
        if (shift > limit - j)
          break;
        j += shift;
      }
    }
  }
  return SALTS_SEARCH_LITERAL_NOT_FOUND;
}

void salts_search_literal_destroy(salts_search_literal_pattern *pattern) {
  if (pattern == NULL)
    return;
  free(pattern->bytes);
  *pattern = (salts_search_literal_pattern){0};
}

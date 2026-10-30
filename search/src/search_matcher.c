#include "search_matcher.h"

#include <limits.h>

static salts_search_matcher_status map_literal(salts_search_literal_status s) {
  switch (s) {
  case SALTS_SEARCH_LITERAL_OK: return SALTS_SEARCH_MATCHER_OK;
  case SALTS_SEARCH_LITERAL_NOT_FOUND: return SALTS_SEARCH_MATCHER_NOT_FOUND;
  case SALTS_SEARCH_LITERAL_INVALID: return SALTS_SEARCH_MATCHER_INVALID;
  case SALTS_SEARCH_LITERAL_LIMIT: return SALTS_SEARCH_MATCHER_LIMIT;
  case SALTS_SEARCH_LITERAL_NO_MEMORY: return SALTS_SEARCH_MATCHER_NO_MEMORY;
  }
  return SALTS_SEARCH_MATCHER_ENGINE_ERROR;
}

static salts_search_matcher_status map_regex(salts_search_regex_status s) {
  switch (s) {
  case SALTS_SEARCH_REGEX_OK: return SALTS_SEARCH_MATCHER_OK;
  case SALTS_SEARCH_REGEX_NOT_FOUND: return SALTS_SEARCH_MATCHER_NOT_FOUND;
  case SALTS_SEARCH_REGEX_INVALID: return SALTS_SEARCH_MATCHER_INVALID;
  case SALTS_SEARCH_REGEX_BAD_PATTERN: return SALTS_SEARCH_MATCHER_BAD_PATTERN;
  case SALTS_SEARCH_REGEX_UNSUPPORTED: return SALTS_SEARCH_MATCHER_UNSUPPORTED;
  case SALTS_SEARCH_REGEX_LIMIT: return SALTS_SEARCH_MATCHER_LIMIT;
  case SALTS_SEARCH_REGEX_NO_MEMORY: return SALTS_SEARCH_MATCHER_NO_MEMORY;
  case SALTS_SEARCH_REGEX_ENGINE_ERROR: return SALTS_SEARCH_MATCHER_ENGINE_ERROR;
  }
  return SALTS_SEARCH_MATCHER_ENGINE_ERROR;
}

salts_search_matcher_status salts_search_matcher_compile(
    salts_search_matcher *out, salts_search_matcher_kind kind,
    const void *pattern, size_t pattern_length, size_t max_pattern_bytes,
    size_t max_subject_bytes) {
  salts_search_matcher_status result;
  if (!out || out->compiled || out->kind != 0 ||
      (kind != SALTS_SEARCH_MATCHER_LITERAL &&
       kind != SALTS_SEARCH_MATCHER_REGEX) ||
      max_pattern_bytes == 0 || max_subject_bytes == 0 ||
      max_subject_bytes > INT_MAX ||
      (pattern_length != 0 && pattern == NULL))
    return SALTS_SEARCH_MATCHER_INVALID;

  if (kind == SALTS_SEARCH_MATCHER_LITERAL) {
    result = map_literal(salts_search_literal_compile(
        &out->compiled_pattern.literal, pattern, pattern_length,
        max_pattern_bytes));
  } else {
    result = map_regex(salts_search_regex_compile(
        &out->compiled_pattern.regex, pattern, pattern_length,
        max_pattern_bytes, max_subject_bytes));
  }
  if (result != SALTS_SEARCH_MATCHER_OK)
    return result;
  out->kind = kind;
  out->max_subject_bytes = max_subject_bytes;
  out->compiled = true;
  return SALTS_SEARCH_MATCHER_OK;
}

salts_search_matcher_status salts_search_matcher_find_first(
    const salts_search_matcher *compiled, const void *subject,
    size_t subject_length, salts_search_matcher_span *out_span) {
  if (out_span)
    *out_span = (salts_search_matcher_span){0};
  if (!compiled || !compiled->compiled || !out_span ||
      (compiled->kind != SALTS_SEARCH_MATCHER_LITERAL &&
       compiled->kind != SALTS_SEARCH_MATCHER_REGEX) ||
      (subject_length != 0 && !subject))
    return SALTS_SEARCH_MATCHER_INVALID;
  if (subject_length > compiled->max_subject_bytes ||
      subject_length > INT_MAX)
    return SALTS_SEARCH_MATCHER_LIMIT;

  if (compiled->kind == SALTS_SEARCH_MATCHER_LITERAL) {
    salts_search_literal_span span = {0};
    const salts_search_matcher_status rc = map_literal(
        salts_search_literal_find(&compiled->compiled_pattern.literal,
                                  subject, subject_length, 0, &span));
    if (rc == SALTS_SEARCH_MATCHER_OK) {
      out_span->byte_start = (uint64_t)span.start;
      out_span->byte_end = (uint64_t)span.end;
    }
    return rc;
  }

  {
    salts_search_regex_span span = {0};
    const salts_search_matcher_status rc = map_regex(
        salts_search_regex_find_first(&compiled->compiled_pattern.regex,
                                      subject, subject_length, &span));
    if (rc == SALTS_SEARCH_MATCHER_OK) {
      out_span->byte_start = span.byte_start;
      out_span->byte_end = span.byte_end;
    }
    return rc;
  }
}

salts_search_matcher_status salts_search_matcher_enumerate_literals(
    const salts_search_matcher *compiled, const void *subject,
    size_t subject_length, bool overlapping, size_t max_hits,
    salts_search_matcher_hit_callback callback, void *user,
    size_t *out_emitted) {
  size_t start_at = 0;
  size_t count = 0;
  if (out_emitted)
    *out_emitted = 0;
  if (!compiled || !compiled->compiled || !callback || !out_emitted ||
      (subject_length != 0 && !subject))
    return SALTS_SEARCH_MATCHER_INVALID;
  if (compiled->kind != SALTS_SEARCH_MATCHER_LITERAL)
    return SALTS_SEARCH_MATCHER_UNSUPPORTED;
  if (subject_length > compiled->max_subject_bytes ||
      subject_length > INT_MAX)
    return SALTS_SEARCH_MATCHER_LIMIT;

  for (;;) {
    salts_search_literal_span found = {0};
    const salts_search_literal_status scan = salts_search_literal_find(
        &compiled->compiled_pattern.literal, subject, subject_length,
        start_at, &found);
    if (scan == SALTS_SEARCH_LITERAL_NOT_FOUND)
      return SALTS_SEARCH_MATCHER_OK; /* all possible hits exhausted */
    if (scan != SALTS_SEARCH_LITERAL_OK)
      return map_literal(scan);
    if (found.start > found.end || found.end > subject_length)
      return SALTS_SEARCH_MATCHER_ENGINE_ERROR;
    if (count == max_hits)
      return SALTS_SEARCH_MATCHER_LIMIT; /* one more hit exists: no fake success */

    {
      const salts_search_matcher_span hit = {
          (uint64_t)found.start, (uint64_t)found.end};
      ++count;
      *out_emitted = count;
      if (!callback(user, &hit))
        return SALTS_SEARCH_MATCHER_STOPPED;
    }
    if (found.start == subject_length)
      return SALTS_SEARCH_MATCHER_OK; /* terminal empty-pattern boundary */

    /* For zero-width matches, advancing by one is mandatory in BOTH modes.
     * For nonempty matches, overlap mode advances from the match start.
     * A compiled pattern and subject are immutable during enumeration. */
    start_at = overlapping || found.start == found.end
                   ? found.start + 1u : found.end;
    if (start_at > subject_length)
      return SALTS_SEARCH_MATCHER_OK;
  }
}

salts_search_matcher_status salts_search_matcher_enumerate_regex(
    const salts_search_matcher *compiled, const void *subject,
    size_t subject_length, bool overlapping, size_t max_hits,
    salts_search_matcher_hit_callback callback, void *user,
    size_t *out_emitted) {
  size_t start_at = 0;
  size_t count = 0;
  if (out_emitted)
    *out_emitted = 0;
  if (!compiled || !compiled->compiled || !callback || !out_emitted ||
      (subject_length != 0 && !subject))
    return SALTS_SEARCH_MATCHER_INVALID;
  if (compiled->kind != SALTS_SEARCH_MATCHER_REGEX)
    return SALTS_SEARCH_MATCHER_UNSUPPORTED;
  if (subject_length > compiled->max_subject_bytes ||
      subject_length > INT_MAX)
    return SALTS_SEARCH_MATCHER_LIMIT;

  for (;;) {
    salts_search_regex_span found = {0};
    const salts_search_matcher_status status = map_regex(
        salts_search_regex_find_from(&compiled->compiled_pattern.regex,
                                     subject, subject_length, start_at, &found));
    if (status == SALTS_SEARCH_MATCHER_NOT_FOUND)
      return SALTS_SEARCH_MATCHER_OK;
    if (status != SALTS_SEARCH_MATCHER_OK)
      return status;
    if (found.byte_start < start_at ||
        found.byte_start > found.byte_end ||
        found.byte_end > subject_length)
      return SALTS_SEARCH_MATCHER_ENGINE_ERROR;
    if (count == max_hits)
      return SALTS_SEARCH_MATCHER_LIMIT; /* extra hit exists: no false success */

    {
      const salts_search_matcher_span hit = {found.byte_start,
                                             found.byte_end};
      ++count;
      *out_emitted = count;
      if (!callback(user, &hit))
        return SALTS_SEARCH_MATCHER_STOPPED;
    }

    /* Preserve original line content for every TRE call. Never reslice.
     * A match at EOF must not be delivered twice; an empty interior match
     * advances exactly one byte, including after a nonempty overlapping hit.
     */
    if (found.byte_start == subject_length)
      return SALTS_SEARCH_MATCHER_OK;
    start_at = (overlapping || found.byte_start == found.byte_end)
                   ? (size_t)found.byte_start + 1u
                   : (size_t)found.byte_end;
    if (start_at > subject_length)
      return SALTS_SEARCH_MATCHER_OK;
  }
}

void salts_search_matcher_destroy(salts_search_matcher *compiled) {
  if (!compiled)
    return;
  if (compiled->compiled && compiled->kind == SALTS_SEARCH_MATCHER_LITERAL)
    salts_search_literal_destroy(&compiled->compiled_pattern.literal);
  else if (compiled->compiled && compiled->kind == SALTS_SEARCH_MATCHER_REGEX)
    salts_search_regex_destroy(&compiled->compiled_pattern.regex);
  *compiled = (salts_search_matcher){0};
}

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

void salts_search_matcher_destroy(salts_search_matcher *compiled) {
  if (!compiled)
    return;
  if (compiled->compiled && compiled->kind == SALTS_SEARCH_MATCHER_LITERAL)
    salts_search_literal_destroy(&compiled->compiled_pattern.literal);
  else if (compiled->compiled && compiled->kind == SALTS_SEARCH_MATCHER_REGEX)
    salts_search_regex_destroy(&compiled->compiled_pattern.regex);
  *compiled = (salts_search_matcher){0};
}

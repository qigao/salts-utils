#include "search_regex.h"
#include <limits.h>
#include <string.h>

#define SALTS_SEARCH_REGEX_HARD_PATTERN_MAX 4096u

salts_search_regex_status salts_search_regex_compile(
    salts_search_regex_pattern *out, const void *pattern_bytes,
    size_t pattern_length, size_t max_pattern_bytes,
    size_t max_subject_bytes) {
  static const char empty[] = "";
  const char *source = (const char *)pattern_bytes;
  int rc;
  if (!out || out->compiled || out->tre.value != NULL ||
      (pattern_length != 0 && pattern_bytes == NULL) ||
      max_pattern_bytes == 0 || max_subject_bytes == 0 ||
      max_subject_bytes > INT_MAX)
    return SALTS_SEARCH_REGEX_INVALID;
  if (pattern_length > max_pattern_bytes ||
      pattern_length > SALTS_SEARCH_REGEX_HARD_PATTERN_MAX)
    return SALTS_SEARCH_REGEX_LIMIT;
  if (!source)
    source = empty;
  rc = tre_regncompb(&out->tre, source, pattern_length, REG_EXTENDED);
  if (rc != REG_OK) {
    /* TRE's failed compilation owns no reusable regex: do not call regfree
     * on a failed object. Discard all partial state before returning. */
    memset(&out->tre, 0, sizeof(out->tre));
    return rc == REG_ESPACE ? SALTS_SEARCH_REGEX_NO_MEMORY
                            : SALTS_SEARCH_REGEX_BAD_PATTERN;
  }
  if (tre_have_backrefs(&out->tre) || tre_have_approx(&out->tre)) {
    tre_regfree(&out->tre);
    memset(&out->tre, 0, sizeof(out->tre));
    return SALTS_SEARCH_REGEX_UNSUPPORTED;
  }
  out->max_subject_bytes = max_subject_bytes;
  out->compiled = true;
  return SALTS_SEARCH_REGEX_OK;
}

salts_search_regex_status salts_search_regex_find_first(
    const salts_search_regex_pattern *compiled, const void *subject,
    size_t subject_length, salts_search_regex_span *out_span) {
  /* A first match is exactly a min_start=0 match on the unchanged subject.
   * Reuse the *same* bounded TRE Tagged-NFA path as repeated exact search;
   * the legacy tre_regnexecb() would bypass the 8 MiB execution cap. */
  return salts_search_regex_find_from(
      compiled, subject, subject_length, 0u, out_span);
}

salts_search_regex_status salts_search_regex_find_from(
    const salts_search_regex_pattern *compiled, const void *subject,
    size_t subject_length, size_t min_start, salts_search_regex_span *out_span) {
  static const char empty[] = "";
  regmatch_t span[1] = {{-1, -1}};
  int rc;
  if (out_span)
    *out_span = (salts_search_regex_span){0};
  if (!compiled || !compiled->compiled || !out_span ||
      (subject_length != 0 && subject == NULL))
    return SALTS_SEARCH_REGEX_INVALID;
  if (subject_length > compiled->max_subject_bytes ||
      subject_length > INT_MAX)
    return SALTS_SEARCH_REGEX_LIMIT;
  if (min_start > subject_length)
    return SALTS_SEARCH_REGEX_NOT_FOUND;
  rc = tre_regnexecb_from(&compiled->tre,
                          subject ? (const char *)subject : empty,
                          subject_length, min_start, 1u, span, 0);
  if (rc == REG_NOMATCH)
    return SALTS_SEARCH_REGEX_NOT_FOUND;
  if (rc == REG_ESPACE)
    return SALTS_SEARCH_REGEX_NO_MEMORY;
  if (rc != REG_OK || span[0].rm_so < 0 ||
      span[0].rm_eo < span[0].rm_so ||
      (size_t)span[0].rm_so < min_start ||
      (size_t)span[0].rm_eo > subject_length)
    return SALTS_SEARCH_REGEX_ENGINE_ERROR;
  out_span->byte_start = (uint64_t)span[0].rm_so;
  out_span->byte_end = (uint64_t)span[0].rm_eo;
  return SALTS_SEARCH_REGEX_OK;
}

void salts_search_regex_destroy(salts_search_regex_pattern *compiled) {
  if (!compiled)
    return;
  if (compiled->compiled)
    tre_regfree(&compiled->tre);
  *compiled = (salts_search_regex_pattern){0};
}

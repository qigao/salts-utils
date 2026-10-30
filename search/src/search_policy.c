#include "search_policy.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static salts_search_policy_status glob_error(salts_search_glob_status e) {
  switch (e) {
  case SALTS_SEARCH_GLOB_OK: return SALTS_SEARCH_POLICY_OK;
  case SALTS_SEARCH_GLOB_LIMIT: return SALTS_SEARCH_POLICY_LIMIT;
  case SALTS_SEARCH_GLOB_NO_MEMORY: return SALTS_SEARCH_POLICY_NO_MEMORY;
  case SALTS_SEARCH_GLOB_BAD_PATTERN: return SALTS_SEARCH_POLICY_BAD_GLOB;
  default: return SALTS_SEARCH_POLICY_INVALID;
  }
}

salts_search_policy_status salts_search_override_init(
    salts_search_override_set *out, salts_search_override_rule *slots,
    size_t slot_capacity, size_t max_pattern_bytes, size_t max_path_bytes,
    bool ascii_case_insensitive) {
  if (out == NULL || out->active || out->rules != NULL || out->count != 0 ||
      slots == NULL || slot_capacity == 0 || max_pattern_bytes == 0 ||
      max_path_bytes == 0 || max_pattern_bytes > SIZE_MAX / 2u - 1u)
    return SALTS_SEARCH_POLICY_INVALID;
  *out = (salts_search_override_set){0};
  out->rules = slots;
  out->capacity = slot_capacity;
  out->max_pattern_bytes = max_pattern_bytes;
  out->max_path_bytes = max_path_bytes;
  out->ascii_case_insensitive = ascii_case_insensitive;
  out->active = true;
  return SALTS_SEARCH_POLICY_OK;
}

salts_search_policy_status salts_search_override_add(
    salts_search_override_set *overrides, const char *pattern,
    size_t pattern_length) {
  salts_search_glob_status rc;
  salts_search_override_rule *rule;
  bool exclude;
  salts_search_glob_scope scope;
  size_t i, end;
  if (!overrides || !overrides->active || !overrides->rules ||
      (pattern == NULL && pattern_length != 0))
    return SALTS_SEARCH_POLICY_INVALID;
  if (pattern_length == 0)
    return SALTS_SEARCH_POLICY_BAD_GLOB;
  if (pattern_length > overrides->max_pattern_bytes ||
      overrides->count >= overrides->capacity)
    return SALTS_SEARCH_POLICY_LIMIT;
  exclude = pattern[0] == '!';
  if (exclude) {
    ++pattern;
    --pattern_length;
  }
  if (pattern_length == 0)
    return SALTS_SEARCH_POLICY_BAD_GLOB;
  /* A CLI glob without an interior slash matches basename components at
   * any depth. A final slash only declares directory-only matching. */
  end = pattern_length;
  if (end > 1 && pattern[end - 1u] == '/')
    --end;
  scope = SALTS_SEARCH_GLOB_COMPONENT;
  for (i = 0; i < end; ++i) {
    if (pattern[i] == '/') {
      scope = SALTS_SEARCH_GLOB_PATH;
      break;
    }
  }
  rule = &overrides->rules[overrides->count];
  *rule = (salts_search_override_rule){0};
  rc = salts_search_glob_compile(&rule->glob, pattern, pattern_length,
                                 scope, overrides->ascii_case_insensitive,
                                 overrides->max_pattern_bytes);
  if (rc != SALTS_SEARCH_GLOB_OK)
    return glob_error(rc);
  rule->excluded = exclude;
  ++overrides->count;
  if (!exclude)
    overrides->has_include = true;
  return SALTS_SEARCH_POLICY_OK;
}

static bool valid_path(const char *path, size_t length) {
  size_t start, i;
  if (!path || length == 0 || path[0] == '/' || path[length - 1u] == '/')
    return false;
  for (start = 0; start < length; start = i + 1u) {
    i = start;
    while (i < length && path[i] != '/') {
      if (path[i] == '\0')
        return false;
      ++i;
    }
    if (i == start || (i - start == 1 && path[start] == '.') ||
        (i - start == 2 && path[start] == '.' && path[start + 1] == '.'))
      return false;
    if (i == length)
      return true;
  }
  return true;
}

static salts_search_policy_status evaluate_one(
    const salts_search_override_set *overrides,
    const salts_search_ignore_set *ignore,
    bool include_hidden, const char *path, size_t length,
    bool is_directory, unsigned char *scratch, size_t scratch_size,
    salts_search_policy_result *out) {
  size_t i;
  bool found_override = false;
  bool override_excluded = false;
  size_t override_index = SIZE_MAX;
  out->decision = SALTS_SEARCH_POLICY_VISIT;
  out->reason = SALTS_SEARCH_POLICY_REASON_NONE;
  out->rule_index = SIZE_MAX;
  if (overrides) {
    for (i = 0; i < overrides->count; ++i) {
      bool matched = false;
      const salts_search_override_rule *r = &overrides->rules[i];
      const char *candidate = path;
      size_t candidate_length = length;
      if (r->glob.scope == SALTS_SEARCH_GLOB_COMPONENT) {
        size_t start = length;
        while (start != 0 && path[start - 1u] != '/')
          --start;
        candidate += start;
        candidate_length -= start;
      }
      const salts_search_glob_status status = salts_search_glob_match(
          &r->glob, candidate, candidate_length, is_directory,
          scratch, scratch_size, &matched);
      if (status != SALTS_SEARCH_GLOB_OK)
        return glob_error(status);
      if (matched) {
        found_override = true;
        override_excluded = r->excluded;
        override_index = i;
      }
    }
    if (found_override || (overrides->has_include && !is_directory)) {
      out->reason = found_override ? SALTS_SEARCH_POLICY_REASON_OVERRIDE
                                   : SALTS_SEARCH_POLICY_REASON_IMPLICIT_OVERRIDE;
      out->rule_index = override_index;
      if (found_override && !override_excluded)
        return SALTS_SEARCH_POLICY_OK; /* overrides whitelist even hidden */
      out->decision = is_directory ? SALTS_SEARCH_POLICY_PRUNE_DIRECTORY
                                   : SALTS_SEARCH_POLICY_SKIP_FILE;
      return SALTS_SEARCH_POLICY_OK;
    }
  }
  if (ignore) {
    salts_search_ignore_decision decision;
    size_t rule_index;
    const salts_search_ignore_status status = salts_search_ignore_evaluate(
        ignore, path, length, is_directory, scratch, scratch_size,
        &decision, &rule_index);
    if (status == SALTS_SEARCH_IGNORE_LIMIT)
      return SALTS_SEARCH_POLICY_LIMIT;
    if (status != SALTS_SEARCH_IGNORE_OK)
      return SALTS_SEARCH_POLICY_INVALID;
    if (decision != SALTS_SEARCH_IGNORE_NONE) {
      out->reason = SALTS_SEARCH_POLICY_REASON_IGNORE;
      out->rule_index = rule_index;
      if (decision == SALTS_SEARCH_IGNORE_INCLUDE)
        return SALTS_SEARCH_POLICY_OK; /* include overrides hidden default */
      out->decision = is_directory ? SALTS_SEARCH_POLICY_PRUNE_DIRECTORY
                                   : SALTS_SEARCH_POLICY_SKIP_FILE;
      return SALTS_SEARCH_POLICY_OK;
    }
  }
  if (!include_hidden) {
    size_t start = length;
    while (start > 0 && path[start - 1u] != '/')
      --start;
    if (path[start] == '.') {
      out->reason = SALTS_SEARCH_POLICY_REASON_HIDDEN;
      out->decision = is_directory ? SALTS_SEARCH_POLICY_PRUNE_DIRECTORY
                                   : SALTS_SEARCH_POLICY_SKIP_FILE;
    }
  }
  return SALTS_SEARCH_POLICY_OK;
}

salts_search_policy_status salts_search_policy_evaluate(
    const salts_search_override_set *overrides,
    const salts_search_ignore_set *ignore_rules,
    const salts_search_policy_options *options,
    const char *relative_path, size_t path_length, bool is_directory,
    unsigned char *scratch, size_t scratch_size,
    salts_search_policy_result *out_result) {
  size_t limit, i;
  salts_search_policy_result candidate = {
      SALTS_SEARCH_POLICY_VISIT, SALTS_SEARCH_POLICY_REASON_NONE, SIZE_MAX};
  salts_search_policy_status status;
  if (out_result)
    *out_result = candidate;
  if (!out_result || !options || options->max_path_bytes == 0 ||
      path_length > options->max_path_bytes || !valid_path(relative_path, path_length) ||
      (overrides && (!overrides->active || path_length > overrides->max_path_bytes)) ||
      (ignore_rules && (!ignore_rules->active ||
                        path_length > ignore_rules->max_path_bytes)) ||
      scratch == NULL)
    return SALTS_SEARCH_POLICY_INVALID;
  limit = 0;
  if (overrides) {
    if (overrides->max_pattern_bytes > SIZE_MAX / 2u - 1u)
      return SALTS_SEARCH_POLICY_INVALID;
    limit = 2u * (overrides->max_pattern_bytes + 1u);
  }
  if (ignore_rules) {
    size_t n;
    if (ignore_rules->max_pattern_bytes > SIZE_MAX / 2u - 1u)
      return SALTS_SEARCH_POLICY_INVALID;
    n = 2u * (ignore_rules->max_pattern_bytes + 1u);
    if (n > limit)
      limit = n;
  }
  if (scratch_size < limit)
    return SALTS_SEARCH_POLICY_LIMIT;
  for (i = 0; i < path_length; ++i) {
    if (relative_path[i] != '/')
      continue;
    status = evaluate_one(overrides, ignore_rules, options->include_hidden,
                          relative_path, i, true, scratch, scratch_size,
                          &candidate);
    if (status != SALTS_SEARCH_POLICY_OK)
      return status;
    if (candidate.decision == SALTS_SEARCH_POLICY_PRUNE_DIRECTORY) {
      *out_result = candidate;
      return SALTS_SEARCH_POLICY_OK;
    }
  }
  status = evaluate_one(overrides, ignore_rules, options->include_hidden,
                        relative_path, path_length, is_directory,
                        scratch, scratch_size, &candidate);
  if (status == SALTS_SEARCH_POLICY_OK)
    *out_result = candidate;
  return status;
}

void salts_search_override_destroy(salts_search_override_set *overrides) {
  if (!overrides)
    return;
  if (overrides->active && overrides->rules) {
    while (overrides->count)
      salts_search_glob_destroy(
          &overrides->rules[--overrides->count].glob);
  }
  *overrides = (salts_search_override_set){0};
}

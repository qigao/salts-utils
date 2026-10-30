#include "search_ignore.h"

#include <stdlib.h>
#include <string.h>

/* A path is already root-relative and normalized by the caller. Reject rather
 * than cleaning untrusted input or silently changing directory provenance. */
static bool is_relative_path(const char *path, size_t len, bool allow_root) {
  size_t i;
  if (path == NULL || len == 0)
    return allow_root && len == 0;
  if (path[0] == '/' || path[len - 1] == '/')
    return false;
  for (i = 0; i < len;) {
    size_t next = i;
    while (next < len && path[next] != '/') {
      if (path[next] == '\0' || path[next] == '\\')
        return false;
      ++next;
    }
    if (next == i || (next - i == 1 && path[i] == '.') ||
        (next - i == 2 && path[i] == '.' && path[i + 1] == '.'))
      return false;
    i = next + (next < len ? 1 : 0);
  }
  return true;
}

static void free_rule(salts_search_ignore_rule *rule) {
  salts_search_glob_destroy(&rule->glob);
  free(rule->scope);
  *rule = (salts_search_ignore_rule){0};
}

salts_search_ignore_status salts_search_ignore_init(
    salts_search_ignore_set *set, salts_search_ignore_rule *rules,
    size_t capacity, size_t max_pattern_bytes, size_t max_file_bytes,
    size_t max_path_bytes, bool ascii_case_insensitive) {
  if (set == NULL || set->active || set->rules || set->count ||
      capacity == 0 || rules == NULL || max_pattern_bytes == 0 ||
      max_file_bytes == 0 || max_path_bytes == 0 ||
      max_pattern_bytes > SIZE_MAX / 2 - 1u)
    return SALTS_SEARCH_IGNORE_INVALID;
  *set = (salts_search_ignore_set){0};
  set->rules = rules;
  set->capacity = capacity;
  set->max_pattern_bytes = max_pattern_bytes;
  set->max_file_bytes = max_file_bytes;
  set->max_path_bytes = max_path_bytes;
  set->ascii_case_insensitive = ascii_case_insensitive;
  set->active = true;
  return SALTS_SEARCH_IGNORE_OK;
}

/* Trim only unescaped trailing spaces: trailing backslash escapes the next
 * space. The parser/Glob compiler retains escapes for final byte matching. */
static size_t trim_unescaped_spaces(const unsigned char *bytes, size_t length) {
  size_t end = length;
  while (end && bytes[end - 1] == ' ') {
    size_t backslashes = 0;
    size_t p = end - 1;
    while (p > 0 && bytes[p - 1] == '\\') {
      --p;
      ++backslashes;
    }
    if (backslashes & 1u)
      break;
    --end;
  }
  return end;
}

static bool contains_slash(const unsigned char *pattern, size_t length) {
  size_t i;
  for (i = 0; i < length; ++i)
    if (pattern[i] == '/')
      return true;
  return false;
}

static salts_search_ignore_status append_rule(
    salts_search_ignore_set *set, salts_search_ignore_source source,
    const char *scope, size_t scope_length, const unsigned char *bytes,
    size_t length, size_t line_number) {
  salts_search_ignore_rule *rule;
  bool negative = false;
  salts_search_glob_scope glob_scope;
  salts_search_glob_status rc;
  if (length == 0 || bytes[0] == '#')
    return SALTS_SEARCH_IGNORE_OK;
  if (bytes[0] == '!') {
    negative = true;
    ++bytes;
    --length;
  }
  if (length == 0)
    return SALTS_SEARCH_IGNORE_BAD_PATTERN;
  if (length > set->max_pattern_bytes)
    return SALTS_SEARCH_IGNORE_LIMIT;
  if (set->count >= set->capacity)
    return SALTS_SEARCH_IGNORE_LIMIT;
  glob_scope = contains_slash(bytes, length)
                   ? SALTS_SEARCH_GLOB_PATH
                   : SALTS_SEARCH_GLOB_COMPONENT;
  rule = &set->rules[set->count];
  /* The caller supplies initially zeroed slots; rollback restores them. */
  *rule = (salts_search_ignore_rule){0};
  rule->scope = (char *)malloc(scope_length + 1u);
  if (!rule->scope)
    return SALTS_SEARCH_IGNORE_NO_MEMORY;
  if (scope_length)
    memcpy(rule->scope, scope, scope_length);
  rule->scope[scope_length] = '\0';
  rc = salts_search_glob_compile(&rule->glob, (const char *)bytes, length,
                                 glob_scope, set->ascii_case_insensitive,
                                 set->max_pattern_bytes);
  if (rc != SALTS_SEARCH_GLOB_OK) {
    free_rule(rule);
    if (rc == SALTS_SEARCH_GLOB_NO_MEMORY)
      return SALTS_SEARCH_IGNORE_NO_MEMORY;
    if (rc == SALTS_SEARCH_GLOB_LIMIT)
      return SALTS_SEARCH_IGNORE_LIMIT;
    return SALTS_SEARCH_IGNORE_BAD_PATTERN;
  }
  rule->scope_length = scope_length;
  rule->line_number = line_number;
  rule->source = source;
  rule->negated = negative;
  ++set->count;
  return SALTS_SEARCH_IGNORE_OK;
}

salts_search_ignore_status salts_search_ignore_add_rules(
    salts_search_ignore_set *set, salts_search_ignore_source source,
    const char *scope, size_t scope_length, const void *bytes,
    size_t byte_count, size_t *out_error_line) {
  const unsigned char *data = (const unsigned char *)bytes;
  size_t start = 0, line = 0, before;
  salts_search_ignore_status rc = SALTS_SEARCH_IGNORE_OK;
  if (out_error_line)
    *out_error_line = 0;
  if (set == NULL || !set->active || set->rules == NULL ||
      out_error_line == NULL || source < SALTS_SEARCH_IGNORE_EXPLICIT_FILE ||
      source > SALTS_SEARCH_IGNORE_RGIGNORE ||
      scope_length > set->max_path_bytes || scope_length == SIZE_MAX ||
      !is_relative_path(scope, scope_length, true) ||
      (source <= SALTS_SEARCH_IGNORE_GIT_INFO && scope_length != 0) ||
      (byte_count && data == NULL))
    return SALTS_SEARCH_IGNORE_INVALID;
  if (byte_count > set->max_file_bytes)
    return SALTS_SEARCH_IGNORE_LIMIT;
  before = set->count;
  while (start < byte_count) {
    size_t end = start, length;
    while (end < byte_count && data[end] != '\n') {
      if (data[end] == '\0') {
        ++line;
        rc = SALTS_SEARCH_IGNORE_BAD_PATTERN;
        goto rollback;
      }
      ++end;
    }
    ++line;
    length = end - start;
    if (length && data[start + length - 1u] == '\r')
      --length; /* Git ignores a trailing CR even on the final line */
    length = trim_unescaped_spaces(data + start, length);
    rc = append_rule(set, source, scope, scope_length, data + start,
                     length, line);
    if (rc != SALTS_SEARCH_IGNORE_OK)
      goto rollback;
    start = end < byte_count ? end + 1u : end;
  }
  return SALTS_SEARCH_IGNORE_OK;
rollback:
  *out_error_line = line;
  while (set->count > before)
    free_rule(&set->rules[--set->count]);
  return rc;
}

/* Rules in a nested directory are only visible to its descendants (not its
 * directory itself); matching is relative to the rule's directory scope. */
static bool rule_covers(const salts_search_ignore_rule *rule,
                        const char *path, size_t length, size_t *relative) {
  if (rule->scope_length == 0) {
    *relative = 0;
    return true;
  }
  if (length <= rule->scope_length ||
      path[rule->scope_length] != '/' ||
      memcmp(path, rule->scope, rule->scope_length) != 0)
    return false;
  *relative = rule->scope_length + 1u;
  return true;
}

static salts_search_ignore_status eval_one(
    const salts_search_ignore_set *set, const char *path, size_t len,
    bool is_directory, unsigned char *scratch, size_t scratch_size,
    salts_search_ignore_decision *result, size_t *winning) {
  size_t i, best_scope = 0;
  salts_search_ignore_source best_source = SALTS_SEARCH_IGNORE_EXPLICIT_FILE;
  bool found = false;
  *result = SALTS_SEARCH_IGNORE_NONE;
  *winning = SIZE_MAX;
  for (i = 0; i < set->count; ++i) {
    const salts_search_ignore_rule *r = &set->rules[i];
    size_t rel;
    bool matched = false;
    salts_search_glob_status status;
    if (!rule_covers(r, path, len, &rel))
      continue;
    status = salts_search_glob_match(&r->glob, path + rel, len - rel,
                                     is_directory, scratch, scratch_size,
                                     &matched);
    if (status == SALTS_SEARCH_GLOB_LIMIT)
      return SALTS_SEARCH_IGNORE_LIMIT;
    if (status != SALTS_SEARCH_GLOB_OK)
      return SALTS_SEARCH_IGNORE_INVALID;
    if (!matched)
      continue;
    if (!found || r->source > best_source ||
        (r->source == best_source && r->scope_length > best_scope) ||
        (r->source == best_source && r->scope_length == best_scope && i > *winning)) {
      best_source = r->source;
      best_scope = r->scope_length;
      *winning = i;
      *result = r->negated ? SALTS_SEARCH_IGNORE_INCLUDE
                           : SALTS_SEARCH_IGNORE_EXCLUDE;
      found = true;
    }
  }
  return SALTS_SEARCH_IGNORE_OK;
}

salts_search_ignore_status salts_search_ignore_evaluate(
    const salts_search_ignore_set *set, const char *path, size_t path_length,
    bool is_directory, unsigned char *scratch, size_t scratch_size,
    salts_search_ignore_decision *out_decision, size_t *out_rule_index) {
  size_t end;
  salts_search_ignore_status rc;
  salts_search_ignore_decision current;
  size_t winner;
  if (out_decision)
    *out_decision = SALTS_SEARCH_IGNORE_NONE;
  if (out_rule_index)
    *out_rule_index = SIZE_MAX;
  if (!set || !set->active || !set->rules || !out_decision ||
      !out_rule_index || !scratch ||
      path_length > set->max_path_bytes ||
      !is_relative_path(path, path_length, false) ||
      scratch_size < 2u * (set->max_pattern_bytes + 1u))
    return SALTS_SEARCH_IGNORE_INVALID;
  for (end = 0; end < path_length; ++end) {
    if (path[end] != '/')
      continue;
    /* Excluded ancestors cannot be re-included by a rule on the leaf. */
    rc = eval_one(set, path, end, true, scratch, scratch_size,
                  &current, &winner);
    if (rc != SALTS_SEARCH_IGNORE_OK)
      return rc;
    if (current == SALTS_SEARCH_IGNORE_EXCLUDE) {
      *out_decision = current;
      *out_rule_index = winner;
      return SALTS_SEARCH_IGNORE_OK;
    }
  }
  return eval_one(set, path, path_length, is_directory,
                  scratch, scratch_size, out_decision, out_rule_index);
}

void salts_search_ignore_destroy(salts_search_ignore_set *set) {
  if (!set)
    return;
  if (set->active && set->rules) {
    while (set->count)
      free_rule(&set->rules[--set->count]);
  }
  *set = (salts_search_ignore_set){0};
}

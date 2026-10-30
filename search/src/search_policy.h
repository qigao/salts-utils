/* Internal C11 path policy: explicit glob overrides, ignore, hidden traversal.
 * Algorithm-only; caller owns arrays/scratch. No filesystem or regex parser.
 */
#ifndef SALTS_SEARCH_POLICY_INTERNAL_H
#define SALTS_SEARCH_POLICY_INTERNAL_H

#include "search_ignore.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum salts_search_policy_status {
  SALTS_SEARCH_POLICY_OK = 0,
  SALTS_SEARCH_POLICY_INVALID,
  SALTS_SEARCH_POLICY_LIMIT,
  SALTS_SEARCH_POLICY_NO_MEMORY,
  SALTS_SEARCH_POLICY_BAD_GLOB
} salts_search_policy_status;

typedef enum salts_search_policy_decision {
  SALTS_SEARCH_POLICY_VISIT = 0,
  SALTS_SEARCH_POLICY_SKIP_FILE,
  SALTS_SEARCH_POLICY_PRUNE_DIRECTORY
} salts_search_policy_decision;

typedef enum salts_search_policy_reason {
  SALTS_SEARCH_POLICY_REASON_NONE = 0,
  SALTS_SEARCH_POLICY_REASON_OVERRIDE,
  SALTS_SEARCH_POLICY_REASON_IMPLICIT_OVERRIDE,
  SALTS_SEARCH_POLICY_REASON_IGNORE,
  SALTS_SEARCH_POLICY_REASON_HIDDEN
} salts_search_policy_reason;

typedef struct salts_search_override_rule {
  salts_search_glob_program glob;
  bool excluded; /* -g '!PATTERN' excludes; -g 'PATTERN' includes */
} salts_search_override_rule;

typedef struct salts_search_override_set {
  salts_search_override_rule *rules; /* caller-owned zeroed slots */
  size_t count;
  size_t capacity;
  size_t max_pattern_bytes;
  size_t max_path_bytes;
  bool ascii_case_insensitive;
  bool has_include;
  bool active;
} salts_search_override_set;

typedef struct salts_search_policy_options {
  /* Bounded normalized relative path, including ancestor scans. */
  size_t max_path_bytes;
  bool include_hidden;
} salts_search_policy_options;

typedef struct salts_search_policy_result {
  salts_search_policy_decision decision;
  salts_search_policy_reason reason;
  /* Index into override or ignore rule array according to reason.
   * SIZE_MAX denotes implicit override, hidden or no applicable rule. */
  size_t rule_index;
} salts_search_policy_result;

salts_search_policy_status salts_search_override_init(
    salts_search_override_set *out, salts_search_override_rule *slots,
    size_t slot_capacity, size_t max_pattern_bytes, size_t max_path_bytes,
    bool ascii_case_insensitive);

/* Compile and append ONE CLI override. '!' is exclusion, unlike Gitignore
 * rule negation. Compiled rules own their source pattern; callers may release
 * the input immediately. No duplicate regex or match implementation.
 */
salts_search_policy_status salts_search_override_add(
    salts_search_override_set *overrides, const char *pattern,
    size_t pattern_length);

/* Returns deterministic per-entry traversal decision. Every ancestor is
 * checked before its descendant, so excluded/hidden directories are pruned
 * even when an explicit child include exists. Explicit glob overrides are
 * evaluated before ignore rules, then hidden policy. An unmatched positive
 * override excludes files but does NOT prune unmatched directories.
 * Optional ignore/overrides inputs are NULL when disabled. Scratch must fit
 * both chosen compiled Glob and ignore budget, or LIMIT is returned.
 */
salts_search_policy_status salts_search_policy_evaluate(
    const salts_search_override_set *overrides,
    const salts_search_ignore_set *ignore_rules,
    const salts_search_policy_options *options,
    const char *relative_path, size_t path_length, bool is_directory,
    unsigned char *scratch, size_t scratch_size,
    salts_search_policy_result *out_result);

void salts_search_override_destroy(salts_search_override_set *overrides);

#endif

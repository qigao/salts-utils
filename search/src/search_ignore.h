/* Private memory-only C11 Gitignore rule collection. Not a public SDK ABI.
 * Each successful add owns its compiled rules, including scope paths.
 * Caller retains the rule vector; mutations and destruction are exclusive.
 * A query borrows caller scratch and is safe concurrently on a frozen set.
 */
#ifndef SALTS_SEARCH_IGNORE_INTERNAL_H
#define SALTS_SEARCH_IGNORE_INTERNAL_H

#include "search_glob.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum salts_search_ignore_status {
  SALTS_SEARCH_IGNORE_OK = 0,
  SALTS_SEARCH_IGNORE_INVALID = 1,
  SALTS_SEARCH_IGNORE_LIMIT = 2,
  SALTS_SEARCH_IGNORE_NO_MEMORY = 3,
  SALTS_SEARCH_IGNORE_BAD_PATTERN = 4
} salts_search_ignore_status;

typedef enum salts_search_ignore_source {
  SALTS_SEARCH_IGNORE_EXPLICIT_FILE = 0,
  SALTS_SEARCH_IGNORE_GIT_GLOBAL = 1,
  SALTS_SEARCH_IGNORE_GIT_INFO = 2,
  SALTS_SEARCH_IGNORE_GITIGNORE = 3,
  SALTS_SEARCH_IGNORE_IGNORE = 4,
  SALTS_SEARCH_IGNORE_RGIGNORE = 5
} salts_search_ignore_source;

typedef enum salts_search_ignore_decision {
  SALTS_SEARCH_IGNORE_NONE = 0,
  SALTS_SEARCH_IGNORE_EXCLUDE = 1,
  SALTS_SEARCH_IGNORE_INCLUDE = 2
} salts_search_ignore_decision;

typedef struct salts_search_ignore_rule {
  salts_search_glob_program glob;
  char *scope; /* owned path, normalized relative to search root, empty=root */
  size_t scope_length;
  size_t line_number;
  salts_search_ignore_source source;
  bool negated;
} salts_search_ignore_rule;

typedef struct salts_search_ignore_set {
  salts_search_ignore_rule *rules; /* caller-owned zeroed rule slots */
  size_t count;
  size_t capacity;
  size_t max_pattern_bytes;
  size_t max_file_bytes;
  size_t max_path_bytes;
  bool ascii_case_insensitive;
  bool active;
} salts_search_ignore_set;

/* No allocation at init; caller owns all array slots until destroy. */
salts_search_ignore_status salts_search_ignore_init(
    salts_search_ignore_set *set, salts_search_ignore_rule *rules,
    size_t capacity, size_t max_pattern_bytes, size_t max_file_bytes,
    size_t max_path_bytes, bool ascii_case_insensitive);

/* Add one complete in-memory file's bytes, no I/O; load atomically.
 * For root/global files, scope_len=0 and scope="". For nested files,
 * scope is an already normalized path to its owning directory.
 * Comments, escaped leading '#'/'!', CRLF, trailing escaped spaces, * / ? /
 * **, and classes use the one existing compiled Glob grammar.
 * On any error, the prior rule collection remains unmodified. The first
 * offending 1-based line is reported; 0 means a file/scope level error.
 */
salts_search_ignore_status salts_search_ignore_add_rules(
    salts_search_ignore_set *set, salts_search_ignore_source source,
    const char *scope, size_t scope_length, const void *bytes,
    size_t byte_count, size_t *out_error_line);

/* path is normalized relative to the same search root. The evaluator checks
 * all ancestor directories FIRST; an excluded parent cannot be re-included
 * by a rule targeting one of its descendants. Does not inspect the filesystem,
 * infer symlinks or decide whether to traverse. out_rule_index=SIZE_MAX if
 * no rule won; it is the first rule that caused an excluded parent or the
 * highest-priority last matching rule for the final entry.
 * scratch_size >= 2*(max_pattern_bytes+1) is sufficient.
 */
salts_search_ignore_status salts_search_ignore_evaluate(
    const salts_search_ignore_set *set, const char *path, size_t path_length,
    bool is_directory, unsigned char *scratch, size_t scratch_size,
    salts_search_ignore_decision *out_decision, size_t *out_rule_index);

void salts_search_ignore_destroy(salts_search_ignore_set *set);

#endif

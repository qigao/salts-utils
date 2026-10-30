/* Internal byte-oriented C11 path glob program. Not a published ABI.
 * No filesystem, regex parser, locale dependency, or implicit I/O.
 */
#ifndef SALTS_SEARCH_GLOB_INTERNAL_H
#define SALTS_SEARCH_GLOB_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum salts_search_glob_status {
  SALTS_SEARCH_GLOB_OK = 0,
  SALTS_SEARCH_GLOB_INVALID = 1,
  SALTS_SEARCH_GLOB_LIMIT = 2,
  SALTS_SEARCH_GLOB_NO_MEMORY = 3,
  SALTS_SEARCH_GLOB_BAD_PATTERN = 4
} salts_search_glob_status;

typedef enum salts_search_glob_scope {
  SALTS_SEARCH_GLOB_PATH = 0, /* whole normalized relative path */
  SALTS_SEARCH_GLOB_COMPONENT = 1 /* any single path component */
} salts_search_glob_scope;

typedef enum salts_search_glob_opcode {
  SALTS_SEARCH_GLOB_LITERAL = 1,
  SALTS_SEARCH_GLOB_ANY,
  SALTS_SEARCH_GLOB_STAR,
  SALTS_SEARCH_GLOB_GLOBSTAR,
  SALTS_SEARCH_GLOB_GLOBSTAR_DIR,
  SALTS_SEARCH_GLOB_CLASS
} salts_search_glob_opcode;

typedef struct salts_search_glob_token {
  salts_search_glob_opcode opcode;
  unsigned char literal;
  unsigned char bitmap[32];
  bool negated;
} salts_search_glob_token;

typedef struct salts_search_glob_program {
  salts_search_glob_token *tokens;
  size_t count;
  salts_search_glob_scope scope;
  bool directory_only;
  bool ascii_case_insensitive;
  bool compiled;
} salts_search_glob_program;

/* The caller must zero-initialize program. It owns a copy of compiled tokens.
 * A malformed class/trailing escape, invalid scope or exceeded budget is an
 * error; no Regex or shell-pattern fallback is attempted.
 */
salts_search_glob_status salts_search_glob_compile(
    salts_search_glob_program *out, const char *pattern, size_t length,
    salts_search_glob_scope scope, bool ascii_case_insensitive,
    size_t max_pattern_bytes);

/* The caller owns scratch (two bitsets with count+1 bytes each). The same
 * compiled program can be used concurrently with independent scratch areas.
 * Paths must use '/' for separators, be relative and contain no NUL. Only
 * ASCII case-insensitive matching is supported; Unicode folding is NOT claimed.
 */
salts_search_glob_status salts_search_glob_match(
    const salts_search_glob_program *program, const char *path,
    size_t path_length, bool is_directory, unsigned char *scratch,
    size_t scratch_bytes, bool *out_matched);

/* Returns a checked minimum scratch size, or 0 if uncompiled/overflow. */
size_t salts_search_glob_scratch_bytes(const salts_search_glob_program *program);
void salts_search_glob_destroy(salts_search_glob_program *program);

#endif

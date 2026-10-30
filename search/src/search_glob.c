#include "search_glob.h"

#include <stdlib.h>
#include <string.h>

static unsigned char ascii_fold(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static void bitmap_set(unsigned char bitmap[32], unsigned char c) {
  bitmap[(size_t)c >> 3] |= (unsigned char)(1u << (c & 7u));
}

static bool bitmap_get(const unsigned char bitmap[32], unsigned char c) {
  return (bitmap[(size_t)c >> 3] & (unsigned char)(1u << (c & 7u))) != 0;
}

/* Add a character and its ASCII case counterpart for folded classes. */
static void class_add(salts_search_glob_token *token, unsigned char c,
                      bool fold) {
  bitmap_set(token->bitmap, c);
  if (fold) {
    if (c >= 'a' && c <= 'z')
      bitmap_set(token->bitmap, (unsigned char)(c - ('a' - 'A')));
    else if (c >= 'A' && c <= 'Z')
      bitmap_set(token->bitmap, (unsigned char)(c + ('a' - 'A')));
  }
}

/* One byte inside bracket class: backslash escapes the next byte. */
static bool class_read(const unsigned char *pattern, size_t end,
                       size_t *index, unsigned char *value) {
  if (*index == end)
    return false;
  if (pattern[*index] == '\\') {
    if (*index + 1u >= end)
      return false;
    ++*index;
  }
  *value = pattern[(*index)++];
  return true;
}

static bool compile_class(const unsigned char *pattern, size_t end,
                          size_t *position, bool fold,
                          salts_search_glob_token *out) {
  size_t i = *position + 1;
  bool have_one = false;
  unsigned char prev = 0;
  memset(out, 0, sizeof(*out));
  out->opcode = SALTS_SEARCH_GLOB_CLASS;
  if (i < end && (pattern[i] == '!' || pattern[i] == '^')) {
    out->negated = true;
    ++i;
  }
  /* A leading ] is literal per typical glob syntax. */
  if (i < end && pattern[i] == ']') {
    class_add(out, ']', fold);
    prev = ']';
    have_one = true;
    ++i;
  }
  while (i < end) {
    unsigned char c;
    if (pattern[i] == ']' && have_one) {
      *position = i + 1;
      return true;
    }
    if (!class_read(pattern, end, &i, &c))
      return false;
    if (c == '-' && have_one && i < end && pattern[i] != ']') {
      unsigned char hi;
      unsigned int x;
      if (!class_read(pattern, end, &i, &hi) || hi < prev)
        return false;
      for (x = prev; x <= hi; ++x)
        class_add(out, (unsigned char)x, fold);
      prev = hi;
      continue;
    }
    class_add(out, c, fold);
    prev = c;
    have_one = true;
  }
  return false; /* missing closing ] */
}

salts_search_glob_status salts_search_glob_compile(
    salts_search_glob_program *out, const char *pattern, size_t length,
    salts_search_glob_scope scope, bool ascii_case_insensitive,
    size_t max_pattern_bytes) {
  const unsigned char *bytes = (const unsigned char *)pattern;
  salts_search_glob_token *tokens;
  size_t i, count = 0, end = length;
  if (out == NULL || out->compiled || out->tokens != NULL ||
      out->count != 0 || (pattern == NULL && length != 0) ||
      max_pattern_bytes == 0 ||
      (scope != SALTS_SEARCH_GLOB_PATH && scope != SALTS_SEARCH_GLOB_COMPONENT))
    return SALTS_SEARCH_GLOB_INVALID;
  if (length > max_pattern_bytes || length > (SIZE_MAX / sizeof(*tokens)) - 1u)
    return SALTS_SEARCH_GLOB_LIMIT;
  if (length == 0)
    return SALTS_SEARCH_GLOB_BAD_PATTERN;
  /* The caller provides an exact bounded raw byte pattern, not a C string. */
  for (i = 0; i < length; ++i) {
    if (bytes[i] == 0)
      return SALTS_SEARCH_GLOB_BAD_PATTERN;
    if (scope == SALTS_SEARCH_GLOB_COMPONENT && bytes[i] == '/' && i + 1u != length)
      return SALTS_SEARCH_GLOB_BAD_PATTERN;
  }
  tokens = (salts_search_glob_token *)calloc(length + 1u, sizeof(*tokens));
  if (tokens == NULL)
    return SALTS_SEARCH_GLOB_NO_MEMORY;
  if (bytes[end - 1] == '/' && end > 1)
    --end;
  if (bytes[0] == '/') { /* root-anchored spelling */
    if (scope == SALTS_SEARCH_GLOB_COMPONENT) {
      free(tokens);
      return SALTS_SEARCH_GLOB_BAD_PATTERN;
    }
    i = 1;
  } else {
    i = 0;
  }
  for (; i < end;) {
    salts_search_glob_token *token = &tokens[count];
    unsigned char c = bytes[i];
    if (c == '\\') {
      ++i;
      if (i == end) {
        free(tokens);
        return SALTS_SEARCH_GLOB_BAD_PATTERN;
      }
      token->opcode = SALTS_SEARCH_GLOB_LITERAL;
      token->literal = ascii_case_insensitive ? ascii_fold(bytes[i]) : bytes[i];
      ++i;
    } else if (c == '?') {
      token->opcode = SALTS_SEARCH_GLOB_ANY;
      ++i;
    } else if (c == '[') {
      if (!compile_class(bytes, end, &i, ascii_case_insensitive, token)) {
        free(tokens);
        return SALTS_SEARCH_GLOB_BAD_PATTERN;
      }
    } else if (c == '*') {
      size_t run = i;
      while (run < end && bytes[run] == '*')
        ++run;
      /* A special ** is only recognized at path segment boundaries. */
      const bool at_boundary = i == 0 || bytes[i - 1] == '/';
      if (run - i == 2u && at_boundary && run < end && bytes[run] == '/') {
        token->opcode = SALTS_SEARCH_GLOB_GLOBSTAR_DIR;
        i = run + 1u;
      } else if (run - i == 2u && at_boundary && run == end) {
        token->opcode = SALTS_SEARCH_GLOB_GLOBSTAR;
        i = run;
      } else {
        /* Consecutive stars in non-special position act like one *. */
        token->opcode = SALTS_SEARCH_GLOB_STAR;
        i = run;
      }
    } else {
      token->opcode = SALTS_SEARCH_GLOB_LITERAL;
      token->literal = ascii_case_insensitive ? ascii_fold(c) : c;
      ++i;
    }
    ++count;
  }
  if (count == 0) {
    free(tokens);
    return SALTS_SEARCH_GLOB_BAD_PATTERN;
  }
  out->tokens = tokens;
  out->count = count;
  out->directory_only = end != length;
  out->scope = scope;
  out->ascii_case_insensitive = ascii_case_insensitive;
  out->compiled = true;
  return SALTS_SEARCH_GLOB_OK;
}

size_t salts_search_glob_scratch_bytes(const salts_search_glob_program *program) {
  if (program == NULL || !program->compiled ||
      program->count > (SIZE_MAX / 2u) - 1u)
    return 0;
  return (program->count + 1u) * 2u;
}

static void epsilon_close(const salts_search_glob_program *program,
                          unsigned char *states) {
  size_t i;
  for (i = 0; i < program->count; ++i) {
    const salts_search_glob_opcode op = program->tokens[i].opcode;
    if (states[i] && (op == SALTS_SEARCH_GLOB_STAR ||
                      op == SALTS_SEARCH_GLOB_GLOBSTAR ||
                      op == SALTS_SEARCH_GLOB_GLOBSTAR_DIR))
      states[i + 1u] = 1;
  }
}

static bool token_accepts(const salts_search_glob_token *token,
                          unsigned char c, bool fold) {
  switch (token->opcode) {
  case SALTS_SEARCH_GLOB_LITERAL:
    return (fold ? ascii_fold(c) : c) == token->literal;
  case SALTS_SEARCH_GLOB_ANY:
    return c != '/';
  case SALTS_SEARCH_GLOB_CLASS: {
    bool found;
    if (c == '/')
      return false;
    found = bitmap_get(token->bitmap, c);
    return token->negated ? !found : found;
  }
  default:
    return false;
  }
}

static bool match_full(const salts_search_glob_program *program,
                       const unsigned char *path, size_t length,
                       unsigned char *scratch) {
  const size_t width = program->count + 1u;
  unsigned char *curr = scratch;
  unsigned char *next = scratch + width;
  size_t p, i;
  memset(curr, 0, width);
  curr[0] = 1;
  epsilon_close(program, curr);
  for (p = 0; p < length; ++p) {
    const unsigned char c = path[p];
    unsigned char *swap;
    memset(next, 0, width);
    for (i = 0; i < program->count; ++i) {
      const salts_search_glob_token *token = &program->tokens[i];
      if (!curr[i])
        continue;
      if (token->opcode == SALTS_SEARCH_GLOB_STAR) {
        if (c != '/')
          next[i] = 1;
      } else if (token->opcode == SALTS_SEARCH_GLOB_GLOBSTAR) {
        next[i] = 1;
      } else if (token->opcode == SALTS_SEARCH_GLOB_GLOBSTAR_DIR) {
        next[i] = 1;
        if (c == '/')
          next[i + 1u] = 1;
      } else if (token_accepts(token, c, program->ascii_case_insensitive)) {
        next[i + 1u] = 1;
      }
    }
    epsilon_close(program, next);
    swap = curr;
    curr = next;
    next = swap;
  }
  return curr[program->count] != 0;
}

salts_search_glob_status salts_search_glob_match(
    const salts_search_glob_program *program, const char *path,
    size_t path_length, bool is_directory, unsigned char *scratch,
    size_t scratch_bytes, bool *out_matched) {
  const unsigned char *p = (const unsigned char *)path;
  size_t i, begin;
  bool matched = false;
  size_t needed = salts_search_glob_scratch_bytes(program);
  if (out_matched != NULL)
    *out_matched = false;
  if (program == NULL || !program->compiled || out_matched == NULL ||
      (path == NULL && path_length != 0))
    return SALTS_SEARCH_GLOB_INVALID;
  if (needed == 0 || scratch == NULL || scratch_bytes < needed)
    return SALTS_SEARCH_GLOB_LIMIT;
  if (path_length == 0 || p[0] == '/' || p[path_length - 1u] == '/')
    return SALTS_SEARCH_GLOB_INVALID;
  for (i = 0; i < path_length; ++i)
    if (p[i] == 0 || (p[i] == '/' && i != 0 && p[i - 1] == '/') ||
        (p[i] == '.' && (i == 0 || p[i - 1] == '/') &&
        (i + 1u == path_length || p[i + 1u] == '/' ||
         (p[i + 1u] == '.' && (i + 2u == path_length || p[i + 2u] == '/')))))
      return SALTS_SEARCH_GLOB_INVALID;
  if (program->directory_only && !is_directory)
    return SALTS_SEARCH_GLOB_OK;
  if (program->scope == SALTS_SEARCH_GLOB_PATH) {
    matched = match_full(program, p, path_length, scratch);
  } else {
    begin = 0;
    for (i = 0; i <= path_length; ++i) {
      if (i == path_length || p[i] == '/') {
        matched = match_full(program, p + begin, i - begin, scratch);
        if (matched)
          break;
        begin = i + 1u;
      }
    }
  }
  *out_matched = matched;
  return SALTS_SEARCH_GLOB_OK;
}

void salts_search_glob_destroy(salts_search_glob_program *program) {
  if (program == NULL)
    return;
  free(program->tokens);
  *program = (salts_search_glob_program){0};
}

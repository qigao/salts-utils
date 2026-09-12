#ifndef JINJA_TEMPLATE_LEXER_H
#define JINJA_TEMPLATE_LEXER_H

#include <stddef.h>
#include <vstr.h>

typedef enum JINJA_TEMPLATE_TOKEN_KIND {
  JINJA_TEMPLATE_TOKEN_EOF = 0,
  JINJA_TEMPLATE_TOKEN_CHARACTER,
  JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN,
  JINJA_TEMPLATE_TOKEN_VARIABLE_CLOSE,
  JINJA_TEMPLATE_TOKEN_STATEMENT_OPEN,
  JINJA_TEMPLATE_TOKEN_STATEMENT_CLOSE,
  JINJA_TEMPLATE_TOKEN_COMMENT_OPEN,
  JINJA_TEMPLATE_TOKEN_COMMENT_CLOSE,
  JINJA_TEMPLATE_TOKEN_ERROR,
  JINJA_TEMPLATE_TOKEN_LINE_STATEMENT,
  JINJA_TEMPLATE_TOKEN_LINE_COMMENT
} JINJA_TEMPLATE_TOKEN_KIND;

enum { JINJA_TEMPLATE_DELIMITER_COUNT = JINJA_TEMPLATE_TOKEN_ERROR - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN };
#ifndef JINJA_TEMPLATE_MAX_DELIMITER_BYTES
#define JINJA_TEMPLATE_MAX_DELIMITER_BYTES 128u
#endif
typedef struct JINJA_TEMPLATE_DELIMITERS {
  /* Indexed by token kind minus VARIABLE_OPEN: variable, statement, comment pairs. */
  vstr tokens[JINJA_TEMPLATE_DELIMITER_COUNT];
  /* NULL data disables a prefix; a non-NULL empty view enables an empty prefix. */
  vstr line_statement_prefix;
  vstr line_comment_prefix;
  /* Private lexical policy; each value must be 0 or 1. Defaults are zero. */
  int trim_blocks;
  int lstrip_blocks;
  int keep_trailing_newline;
} JINJA_TEMPLATE_DELIMITERS;
extern const JINJA_TEMPLATE_DELIMITERS JINJA_TEMPLATE_DEFAULT_DELIMITERS;

typedef struct JINJA_TEMPLATE_TOKEN {
  JINJA_TEMPLATE_TOKEN_KIND kind;
  size_t offset;
  size_t length;
} JINJA_TEMPLATE_TOKEN;

typedef struct JINJA_TEMPLATE_LEXER {
  const unsigned char *input;
  const unsigned char *cursor;
  const unsigned char *limit;
  const JINJA_TEMPLATE_DELIMITERS *delimiters;
  size_t delimiter_ranks[JINJA_TEMPLATE_DELIMITER_COUNT];
  size_t line_statement_rank;
  size_t line_comment_rank;
  size_t line_scan;
  int line_start;
  int previous_nonspace;
} JINJA_TEMPLATE_LEXER;

void jinja_template_lexer_init(JINJA_TEMPLATE_LEXER *lexer, const char *input, size_t length,
    const JINJA_TEMPLATE_DELIMITERS *delimiters);
void jinja_template_lexer_set_offset(JINJA_TEMPLATE_LEXER *lexer, size_t offset);
int jinja_template_lexer_next(JINJA_TEMPLATE_LEXER *lexer, JINJA_TEMPLATE_TOKEN *token);
int jinja_template_lexer_open_next(JINJA_TEMPLATE_LEXER *lexer, JINJA_TEMPLATE_TOKEN *token);
/* Raw mode must preserve overlapping opening braces instead of consuming {{ or {#. */
int jinja_template_lexer_raw_next(JINJA_TEMPLATE_LEXER *lexer, JINJA_TEMPLATE_TOKEN *token);

/* Private, validated views: match against CRLF/CR-as-LF source without rewriting
 * bytes. Return original byte end or SIZE_MAX; configuration is not normalized.
 * Empty delimiters match at a physical character boundary (enabled line prefixes). */
size_t jinja_template_delimiter_end(vstr source, size_t offset, vstr delimiter);

#endif

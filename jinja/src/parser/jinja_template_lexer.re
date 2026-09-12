// re2c --lang c
#include "jinja_template_lexer.h"
#include <string.h>
#include <salts_unicode.h>

const JINJA_TEMPLATE_DELIMITERS JINJA_TEMPLATE_DEFAULT_DELIMITERS = {.tokens = {
    {.data = "{{", .len = 2u}, {.data = "}}", .len = 2u},
    {.data = "{%", .len = 2u}, {.data = "%}", .len = 2u},
    {.data = "{#", .len = 2u}, {.data = "#}", .len = 2u}}};

static size_t jinja_template_prefix_rank(vstr prefix) {
  size_t cursor = 0u, count = 0u;
  while (cursor < prefix.len) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(prefix, &cursor, &scalar) != SALTS_UNICODE_OK) return SIZE_MAX;
    ++count;
  }
  return count;
}

size_t jinja_template_delimiter_end(vstr source, size_t offset, vstr delimiter) {
  if (offset > source.len || (offset != 0u && offset < source.len &&
      source.data[offset] == '\n' && source.data[offset - 1u] == '\r')) return SIZE_MAX;
  /* O(delimiter bytes), O(1) space. Consume a physical CRLF atomically so spans
   * remain in original coordinates while matching Jinja's normalized source. */
  for (size_t i = 0u; i < delimiter.len; ++i) {
    if (offset == source.len) return SIZE_MAX;
    char ch = source.data[offset++];
    if (ch == '\r') {
      if (offset < source.len && source.data[offset] == '\n') ++offset;
      ch = '\n';
    }
    if (ch != delimiter.data[i]) return SIZE_MAX;
  }
  return offset;
}

void jinja_template_lexer_init(JINJA_TEMPLATE_LEXER *lexer, const char *input, size_t length,
    const JINJA_TEMPLATE_DELIMITERS *delimiters) {
  static const unsigned char empty_input[] = {0u};
  if (lexer == NULL) return;
  lexer->input = input != NULL ? (const unsigned char *)input : empty_input;
  lexer->cursor = lexer->input;
  lexer->limit = lexer->input + length;
  lexer->delimiters = delimiters;
  if (delimiters != NULL) {
    for (size_t i = 0u; i < JINJA_TEMPLATE_DELIMITER_COUNT; ++i)
      lexer->delimiter_ranks[i] = jinja_template_prefix_rank(delimiters->tokens[i]);
    lexer->line_statement_rank = jinja_template_prefix_rank(delimiters->line_statement_prefix);
    lexer->line_comment_rank = jinja_template_prefix_rank(delimiters->line_comment_prefix);
  }
  lexer->line_scan = 0u;
  lexer->line_start = 1;
  lexer->previous_nonspace = 0;
}

void jinja_template_lexer_set_offset(JINJA_TEMPLATE_LEXER *lexer, size_t offset) {
  if (lexer == NULL) return;
  lexer->cursor = lexer->input + offset;
}

typedef enum JINJA_TEMPLATE_SCAN_MODE {
  JINJA_TEMPLATE_SCAN_ALL, JINJA_TEMPLATE_SCAN_OPEN, JINJA_TEMPLATE_SCAN_RAW
} JINJA_TEMPLATE_SCAN_MODE;

static int jinja_template_lex_space(salts_unicode_scalar scalar) {
  enum { SEPARATOR_FIRST = 0x1c, SEPARATOR_LAST = 0x1f };
  return (scalar.properties & SALTS_UNICODE_PROPERTY_WHITE_SPACE) != 0u ||
      (scalar.value >= SEPARATOR_FIRST && scalar.value <= SEPARATOR_LAST);
}

/* Derived context advances once across consumed source, including skipped tags.
 * Raw-mode speculative seeks do not update it. Explicit backward seeks rebuild. */
static int jinja_template_line_context(JINJA_TEMPLATE_LEXER *lexer, size_t offset) {
  vstr source = vstr_from_buf((const char *)lexer->input, (size_t)(lexer->limit - lexer->input));
  if (offset < lexer->line_scan) {
    lexer->line_scan = 0u;
    lexer->line_start = 1;
    lexer->previous_nonspace = 0;
  }
  while (lexer->line_scan < offset) {
    size_t next = lexer->line_scan;
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(source, &next, &scalar) != SALTS_UNICODE_OK || next > offset) return 0;
    lexer->line_scan = next;
    lexer->line_start = scalar.value == '\n' || scalar.value == '\r';
    lexer->previous_nonspace = !jinja_template_lex_space(scalar);
  }
  return 1;
}

static size_t jinja_template_line_prefix(JINJA_TEMPLATE_LEXER *lexer, size_t offset, vstr prefix, int statement) {
  if (prefix.data == NULL || (statement ? !lexer->line_start : (!lexer->line_start && !lexer->previous_nonspace)))
    return SIZE_MAX;
  vstr source = vstr_from_buf((const char *)lexer->input, (size_t)(lexer->limit - lexer->input));
  size_t cursor = offset;
  size_t match = SIZE_MAX;
  for (;;) {
    size_t end = jinja_template_delimiter_end(source, cursor, prefix);
    if (end != SIZE_MAX) match = end - offset;
    if (cursor == source.len) break;
    size_t next = cursor;
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(source, &next, &scalar) != SALTS_UNICODE_OK) return SIZE_MAX;
    if (statement ? (scalar.value != ' ' && scalar.value != '\t' && scalar.value != '\v') :
        (!jinja_template_lex_space(scalar) || scalar.value == '\r' || scalar.value == '\n')) break;
    cursor = next;
  }
  return match;
}

/* Configuration is validated by the admitting parser. One source of delimiter
 * truth serves all lexical modes. O(bytes * total delimiter bytes), with a
 * fixed six-token table and bounded delimiter lengths; no source rewriting. */
static int jinja_template_scan(JINJA_TEMPLATE_LEXER *lexer, JINJA_TEMPLATE_TOKEN *token,
    JINJA_TEMPLATE_SCAN_MODE mode) {
  if (lexer == NULL || token == NULL || lexer->delimiters == NULL) return -1;
  const unsigned char *YYCURSOR = lexer->cursor;
  const unsigned char *YYLIMIT = lexer->limit;
  const unsigned char *token_start = YYCURSOR;
  const vstr source = vstr_from_buf((const char *)lexer->input, (size_t)(YYLIMIT - lexer->input));
  token->kind = JINJA_TEMPLATE_TOKEN_CHARACTER;
scan:
  if (YYCURSOR < YYLIMIT) {
    size_t longest = 0u;
    size_t consumed = 0u;
    JINJA_TEMPLATE_TOKEN_KIND selected = JINJA_TEMPLATE_TOKEN_CHARACTER;
    for (size_t i = 0u; i < JINJA_TEMPLATE_DELIMITER_COUNT; ++i) {
      JINJA_TEMPLATE_TOKEN_KIND kind = (JINJA_TEMPLATE_TOKEN_KIND)(i + JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN);
      if (mode == JINJA_TEMPLATE_SCAN_RAW && kind != JINJA_TEMPLATE_TOKEN_STATEMENT_OPEN) continue;
      if (mode == JINJA_TEMPLATE_SCAN_OPEN && i % 2u != 0u) continue;
      vstr delimiter = lexer->delimiters->tokens[i];
      size_t rank = lexer->delimiter_ranks[i];
      if (rank == SIZE_MAX) return -1;
      size_t offset = (size_t)(YYCURSOR - lexer->input);
      size_t end = rank > longest ? jinja_template_delimiter_end(source, offset, delimiter) : SIZE_MAX;
      if (end != SIZE_MAX) {
        longest = rank;
        consumed = end - offset;
        selected = kind;
      }
    }
    if (mode == JINJA_TEMPLATE_SCAN_OPEN &&
        (lexer->delimiters->line_statement_prefix.data != NULL || lexer->delimiters->line_comment_prefix.data != NULL) &&
        jinja_template_line_context(lexer, (size_t)(YYCURSOR - lexer->input))) {
      vstr prefixes[] = {lexer->delimiters->line_statement_prefix, lexer->delimiters->line_comment_prefix};
      size_t ranks[] = {lexer->line_statement_rank, lexer->line_comment_rank};
      for (size_t i = 0u; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        if (ranks[i] == SIZE_MAX) return -1;
        size_t length = jinja_template_line_prefix(lexer, (size_t)(YYCURSOR - lexer->input), prefixes[i], i == 0u);
        /* An empty comment before a newline has no payload; leave that newline
         * in the text stream rather than publishing a zero-progress token. */
        if (i != 0u && length == 0u && (*YYCURSOR == '\r' || *YYCURSOR == '\n')) continue;
        if (length != SIZE_MAX && (selected == JINJA_TEMPLATE_TOKEN_CHARACTER || ranks[i] > longest ||
            (ranks[i] == longest && selected != JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN &&
             selected != JINJA_TEMPLATE_TOKEN_LINE_STATEMENT))) {
          longest = ranks[i];
          consumed = length;
          selected = i == 0u ? JINJA_TEMPLATE_TOKEN_LINE_STATEMENT : JINJA_TEMPLATE_TOKEN_LINE_COMMENT;
        }
      }
    }
    if (selected != JINJA_TEMPLATE_TOKEN_CHARACTER) {
      if (YYCURSOR == token_start) { token->kind = selected; YYCURSOR += consumed; }
      goto accept;
    }
  }
  /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:yyfill:enable = 0;
    re2c:eof = 0;
    re2c:api = custom;
    re2c:api:style = free-form;
    re2c:define:YYLESSTHAN = "YYCURSOR >= YYLIMIT";
    re2c:define:YYPEEK = "YYCURSOR < YYLIMIT ? *YYCURSOR : 0";
    re2c:define:YYSKIP = "++YYCURSOR;";
    $ { if (YYCURSOR == token_start) token->kind = JINJA_TEMPLATE_TOKEN_EOF; goto accept; }
    "\r\n" { goto scan; }
    * { goto scan; }
  */
accept:
  token->offset = (size_t)(token_start - lexer->input);
  token->length = (size_t)(YYCURSOR - token_start);
  lexer->cursor = YYCURSOR;
  return token->kind == JINJA_TEMPLATE_TOKEN_EOF ? 0 : 1;
}

int jinja_template_lexer_next(JINJA_TEMPLATE_LEXER *lexer, JINJA_TEMPLATE_TOKEN *token) {
  return jinja_template_scan(lexer, token, JINJA_TEMPLATE_SCAN_ALL);
}
int jinja_template_lexer_open_next(JINJA_TEMPLATE_LEXER *lexer, JINJA_TEMPLATE_TOKEN *token) {
  return jinja_template_scan(lexer, token, JINJA_TEMPLATE_SCAN_OPEN);
}
int jinja_template_lexer_raw_next(JINJA_TEMPLATE_LEXER *lexer, JINJA_TEMPLATE_TOKEN *token) {
  return jinja_template_scan(lexer, token, JINJA_TEMPLATE_SCAN_RAW);
}

// re2c --lang c
#include "jinja_text_lexer.h"

/*!include:re2c "unicode_categories.re" */

int jinja_text_wordcount(vstr input, size_t *count) {
  *count = 0u;
  if (input.len == 0u) return 1;
  const char *YYCURSOR = input.data;
  const char *YYLIMIT = input.data + input.len;
  const char *YYMARKER;
scan:
  /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:encoding:utf8 = 1;
    re2c:encoding-policy = fail;
    re2c:yyfill:enable = 0;
    re2c:eof = 0;
    re2c:api = custom;
    re2c:api:style = free-form;
    re2c:define:YYLESSTHAN = "YYCURSOR >= YYLIMIT";
    re2c:define:YYPEEK = "YYCURSOR < YYLIMIT ? (unsigned char)*YYCURSOR : 0";
    re2c:define:YYSKIP = "++YYCURSOR;";
    re2c:define:YYBACKUP = "YYMARKER = YYCURSOR;";
    re2c:define:YYRESTORE = "YYCURSOR = YYMARKER;";

    (L | N | "_")+ { ++*count; goto scan; }
    [\u0000-\ud7ff\ue000-\U0010ffff] { goto scan; }
    $ { return 1; }
    * { return 0; }
  */
}

enum { WRAP_BREAK = 1u, WRAP_SPACE = 2u, WRAP_WORD = 4u,
       WRAP_LETTER = 8u, WRAP_PUNCT = 16u, WRAP_HYPHEN = 32u };

int jinja_text_format(vstr input, size_t *offset, JINJA_TEXT_FORMAT *field) {
  const char *YYCURSOR = input.data + *offset, *YYLIMIT = input.data + input.len;
  const char *start;
  *field = (JINJA_TEXT_FORMAT){0};
  if (YYCURSOR < YYLIMIT && *YYCURSOR == '(') {
    start = ++YYCURSOR;
    size_t nesting = 1u;
    while (YYCURSOR < YYLIMIT && nesting != 0u) {
      if (*YYCURSOR == '(') ++nesting;
      else if (*YYCURSOR == ')') --nesting;
      ++YYCURSOR;
    }
    if (nesting != 0u) return 0;
    field->key = vstr_from_buf(start, (size_t)(YYCURSOR - start) - 1u);
  }
  start = YYCURSOR;
  /*!re2c
    [#0+\- ]* { field->flags = vstr_from_buf(start, (size_t)(YYCURSOR - start)); goto width; }
  */
width:
  start = YYCURSOR;
  /*!re2c
    ([0-9]+ | "*") { field->width = vstr_from_buf(start, (size_t)(YYCURSOR - start)); goto precision; }
    * { YYCURSOR = start; goto precision; }
    $ { return 0; }
  */
precision:
  if (YYCURSOR < YYLIMIT && *YYCURSOR == '.') {
    start = ++YYCURSOR;
    /*!re2c
      ([0-9]* | "*") { field->precision = vstr_from_buf(start, (size_t)(YYCURSOR - start)); goto conversion; }
    */
  }
conversion:
  /*!re2c
    [hlL]? [diuoxXeEfFgGcrsa] {
      field->conversion = YYCURSOR[-1];
      *offset = (size_t)(YYCURSOR - input.data);
      return 1;
    }
    * { return 0; }
    $ { return 0; }
  */
}

static unsigned jinja_wrap_character(const char **cursor, const char *YYLIMIT) {
  const char *YYCURSOR = *cursor, *YYMARKER;
  unsigned kind;
  /*!re2c
    Nd { kind = WRAP_WORD | WRAP_PUNCT; goto done; }
    (L | N | "_") { kind = WRAP_WORD | WRAP_LETTER | WRAP_PUNCT; goto done; }
    [!"'&.,?] { kind = WRAP_PUNCT; goto done; }
    [ \t\n\v\f\r] { kind = WRAP_BREAK | WRAP_SPACE; goto done; }
    (Zs | [\u001c-\u001f\u0085\u2028\u2029]) { kind = WRAP_SPACE; goto done; }
    "-" { kind = WRAP_HYPHEN; goto done; }
    [\u0000-\ud7ff\ue000-\U0010ffff] { kind = 0u; goto done; }
    * { kind = 0u; goto done; }
    $ { return 0u; }
  */
done:
  *cursor = YYCURSOR;
  return kind;
}

static int jinja_wrap_letters_follow(const char *cursor, const char *end) {
  if (!(jinja_wrap_character(&cursor, end) & WRAP_LETTER)) return 0;
  unsigned next = jinja_wrap_character(&cursor, end);
  if (next & WRAP_HYPHEN) next = jinja_wrap_character(&cursor, end);
  return (next & WRAP_LETTER) != 0u;
}

int jinja_text_wrap_next(JINJA_TEXT_WRAP_SCAN *scan, JINJA_TEXT_CHUNK *chunk) {
  if (scan->offset == scan->input.len) return 0;
  const char *begin = scan->input.data + scan->offset, *cursor = begin;
  const char *end = scan->input.data + scan->input.len;
  size_t characters = 0u, content_end = 0u;
  unsigned whitespace = 0u;
  while (cursor < end) {
    const char *next = cursor;
    unsigned kind = jinja_wrap_character(&next, end);
    if (characters == 0u) whitespace = kind & WRAP_BREAK;
    else if ((kind & WRAP_BREAK) != whitespace) break;
    if (scan->hyphens && (kind & WRAP_HYPHEN) && (scan->previous[0] & WRAP_PUNCT)) {
      const char *dashes = next;
      while (dashes < end && *dashes == '-') ++dashes;
      const char *look = dashes;
      if (dashes != next && (jinja_wrap_character(&look, end) & WRAP_WORD)) {
        if (characters != 0u) break;
        characters = (size_t)(dashes - cursor);
        cursor = dashes;
        content_end = characters;
        scan->previous[0] = scan->previous[1] = WRAP_HYPHEN;
        scan->previous[2] = 0u;
        break;
      }
    }
    int split = scan->hyphens && characters != 0u && (kind & WRAP_HYPHEN) &&
        (scan->previous[0] & WRAP_LETTER) &&
        ((scan->previous[1] & WRAP_LETTER) ||
          ((scan->previous[1] & WRAP_HYPHEN) && (scan->previous[2] & WRAP_LETTER))) &&
        jinja_wrap_letters_follow(next, end);
    scan->previous[2] = scan->previous[1];
    scan->previous[1] = scan->previous[0];
    scan->previous[0] = kind;
    ++characters;
    cursor = next;
    if (!(kind & WRAP_SPACE)) content_end = (size_t)(cursor - begin);
    if (split) break;
  }
  *chunk = (JINJA_TEXT_CHUNK){vstr_from_buf(begin, (size_t)(cursor - begin)), characters, content_end};
  scan->offset += chunk->text.len;
  return 1;
}

JINJA_TEXT_CHUNK jinja_text_wrap_split(JINJA_TEXT_CHUNK *chunk, size_t width, int hyphens) {
  const char *cursor = chunk->text.data, *end = cursor + chunk->text.len;
  JINJA_TEXT_CHUNK prefix = {.text = {cursor, 0u}}, at_hyphen = {0};
  int non_hyphen = 0;
  for (size_t i = 0u; i < width; ++i) {
    unsigned kind = jinja_wrap_character(&cursor, end);
    prefix.text.len = (size_t)(cursor - chunk->text.data);
    ++prefix.characters;
    if (!(kind & WRAP_SPACE)) prefix.content_end = prefix.text.len;
    if (hyphens && (kind & WRAP_HYPHEN) && non_hyphen) at_hyphen = prefix;
    if (!(kind & WRAP_HYPHEN)) non_hyphen = 1;
  }
  if (at_hyphen.characters != 0u) prefix = at_hyphen;
  chunk->text.data += prefix.text.len;
  chunk->text.len -= prefix.text.len;
  chunk->characters -= prefix.characters;
  chunk->content_end = chunk->content_end > prefix.text.len ? chunk->content_end - prefix.text.len : 0u;
  return prefix;
}

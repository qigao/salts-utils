#ifndef JINJA_TEXT_LEXER_H
#define JINJA_TEXT_LEXER_H

#include <vstr.h>

/* Borrowed UTF-8. Returns one on success, zero for invalid UTF-8.
 * Counts Unicode17 (L|N|_)+ runs in O(bytes) time and constant space. */
int jinja_text_wordcount(vstr input, size_t *count);

typedef struct JINJA_TEXT_FORMAT {
  vstr key, flags, width, precision;
  char conversion;
} JINJA_TEXT_FORMAT;

/* Scan one %-field after its opening percent. Views borrow validated UTF-8 input;
 * NULL key/precision means absent. Returns zero for malformed format syntax. */
int jinja_text_format(vstr input, size_t *offset, JINJA_TEXT_FORMAT *field);

enum { JINJA_TEXT_WRAP_HISTORY = 3 };
typedef struct JINJA_TEXT_WRAP_SCAN {
  vstr input;
  size_t offset;
  unsigned previous[JINJA_TEXT_WRAP_HISTORY];
  int hyphens;
} JINJA_TEXT_WRAP_SCAN;

typedef struct JINJA_TEXT_CHUNK {
  vstr text;
  size_t characters;
  /* Byte end of the last non-whitespace scalar; zero means an all-blank chunk. */
  size_t content_end;
} JINJA_TEXT_CHUNK;

/* Private, validated UTF-8 paragraph. Zero-initialize the scan, then set input
 * and hyphens. Returned views borrow input; zero means exhausted. O(bytes)/O(1). */
int jinja_text_wrap_next(JINJA_TEXT_WRAP_SCAN *scan, JINJA_TEXT_CHUNK *chunk);
/* Split an already scanned chunk at at most width characters, preferring a
 * preceding hyphen when requested. Advances chunk and returns its borrowed prefix. */
JINJA_TEXT_CHUNK jinja_text_wrap_split(JINJA_TEXT_CHUNK *chunk, size_t width, int hyphens);

#endif

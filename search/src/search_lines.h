/* Private C11 streaming line delineator. Not an installed/public ABI.
 * Source bytes are borrowed for feed only, output views until callback returns.
 * One driver owns a scanner; no concurrent/reentrant feed, finish or destroy.
 */
#ifndef SALTS_SEARCH_LINES_INTERNAL_H
#define SALTS_SEARCH_LINES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum salts_search_lines_status {
  SALTS_SEARCH_LINES_OK = 0,
  SALTS_SEARCH_LINES_STOPPED = 1,
  SALTS_SEARCH_LINES_INVALID = 2,
  SALTS_SEARCH_LINES_LIMIT = 3,
  SALTS_SEARCH_LINES_OVERFLOW = 4,
  SALTS_SEARCH_LINES_BINARY = 5,
  SALTS_SEARCH_LINES_CLOSED = 6
} salts_search_lines_status;

typedef enum salts_search_lines_newline {
  SALTS_SEARCH_LINES_LF = 0,       /* LF / CRLF; lone CR is content */
  SALTS_SEARCH_LINES_UNIVERSAL = 1 /* LF / CRLF / lone CR */
} salts_search_lines_newline;

typedef enum salts_search_lines_binary {
  SALTS_SEARCH_LINES_BINARY_ALLOW = 0,
  SALTS_SEARCH_LINES_BINARY_REJECT_NUL = 1
} salts_search_lines_binary;

typedef struct salts_search_line {
  const unsigned char *bytes; /* borrowed only during callback */
  size_t length;              /* excludes line terminator */
  uint64_t byte_start;
  uint64_t byte_end;          /* exclusive; excludes line terminator */
  uint64_t line_number;       /* one-based */
  unsigned terminator_size;   /* 0=EOF, 1=LF or CR, 2=CRLF */
} salts_search_line;

typedef bool (*salts_search_line_callback)(void *user,
                                           const salts_search_line *line);

typedef struct salts_search_line_scanner {
  unsigned char *line_buffer;
  size_t line_capacity;
  size_t line_size;
  uint64_t byte_offset;
  uint64_t line_start;
  uint64_t line_number;
  salts_search_line_callback on_line;
  void *callback_user;
  salts_search_lines_newline newline_mode;
  salts_search_lines_binary binary_mode;
  bool pending_cr;
  bool active;
  bool closed;
} salts_search_line_scanner;

/* Caller owns exactly line_capacity bytes until scanner closes.
 * The scanner never allocates, grows the buffer or aliases source memory.
 * Zero capacity is valid for empty lines only; no callback may reenter scanner.
 */
salts_search_lines_status salts_search_lines_init(
    salts_search_line_scanner *scanner, unsigned char *line_buffer,
    size_t line_capacity, salts_search_lines_newline newline_mode,
    salts_search_lines_binary binary_mode, salts_search_line_callback on_line,
    void *callback_user);

/* out_consumed counts only bytes consumed from THIS call; set even on STOP.
 * LIMIT/BINARY/OVERFLOW terminally closes the scanner with explicit status;
 * already delivered lines are not rolled back. Empty chunks are valid.
 */
salts_search_lines_status salts_search_lines_feed(
    salts_search_line_scanner *scanner, const void *bytes, size_t byte_count,
    size_t *out_consumed);

/* Publish a final unterminated nonempty line if present; never an extra line
 * after a terminal delimiter. Exactly one finish is permitted.
 */
salts_search_lines_status salts_search_lines_finish(
    salts_search_line_scanner *scanner);

#endif

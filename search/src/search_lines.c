#include "search_lines.h"

#include <limits.h>

/* Once the caller can no longer trust the stream, no later feed may succeed. */
static salts_search_lines_status lines_abort(salts_search_line_scanner *scanner,
                                            salts_search_lines_status reason) {
  scanner->closed = true;
  return reason;
}

static salts_search_lines_status lines_append(salts_search_line_scanner *scanner,
                                              unsigned char byte) {
  if (scanner->line_size == scanner->line_capacity)
    return lines_abort(scanner, SALTS_SEARCH_LINES_LIMIT);
  scanner->line_buffer[scanner->line_size++] = byte;
  return SALTS_SEARCH_LINES_OK;
}

static salts_search_lines_status lines_emit(salts_search_line_scanner *scanner,
                                            unsigned terminator_size) {
  salts_search_line record;
  bool keep_going;
  if (scanner->line_number == UINT64_MAX ||
      (uint64_t)scanner->line_size > UINT64_MAX - scanner->line_start)
    return lines_abort(scanner, SALTS_SEARCH_LINES_OVERFLOW);
  record.bytes = scanner->line_buffer;
  record.length = scanner->line_size;
  record.byte_start = scanner->line_start;
  record.byte_end = scanner->line_start + (uint64_t)scanner->line_size;
  record.line_number = scanner->line_number;
  record.terminator_size = terminator_size;
  /* A delimiter has already been consumed, so byte_end includes no CR/LF. */
  if ((uint64_t)terminator_size != scanner->byte_offset - record.byte_end)
    return lines_abort(scanner, SALTS_SEARCH_LINES_INVALID);
  keep_going = scanner->on_line(scanner->callback_user, &record);
  scanner->line_size = 0;
  scanner->line_start = scanner->byte_offset;
  ++scanner->line_number;
  if (!keep_going)
    return lines_abort(scanner, SALTS_SEARCH_LINES_STOPPED);
  return SALTS_SEARCH_LINES_OK;
}

salts_search_lines_status salts_search_lines_init(
    salts_search_line_scanner *scanner, unsigned char *line_buffer,
    size_t line_capacity, salts_search_lines_newline newline_mode,
    salts_search_lines_binary binary_mode, salts_search_line_callback on_line,
    void *callback_user) {
  if (scanner == NULL || scanner->active || scanner->closed ||
      scanner->line_buffer != NULL || scanner->line_size != 0 ||
      scanner->byte_offset != 0 || scanner->line_start != 0 ||
      scanner->line_number != 0 ||
      (line_capacity != 0 && line_buffer == NULL) || on_line == NULL ||
      (newline_mode != SALTS_SEARCH_LINES_LF &&
       newline_mode != SALTS_SEARCH_LINES_UNIVERSAL) ||
      (binary_mode != SALTS_SEARCH_LINES_BINARY_ALLOW &&
       binary_mode != SALTS_SEARCH_LINES_BINARY_REJECT_NUL))
    return SALTS_SEARCH_LINES_INVALID;
  scanner->line_buffer = line_buffer;
  scanner->line_capacity = line_capacity;
  scanner->line_number = 1;
  scanner->newline_mode = newline_mode;
  scanner->binary_mode = binary_mode;
  scanner->on_line = on_line;
  scanner->callback_user = callback_user;
  scanner->pending_cr = false;
  scanner->closed = false;
  scanner->active = true;
  return SALTS_SEARCH_LINES_OK;
}

salts_search_lines_status salts_search_lines_feed(
    salts_search_line_scanner *scanner, const void *bytes, size_t byte_count,
    size_t *out_consumed) {
  const unsigned char *data = (const unsigned char *)bytes;
  size_t i;
  if (out_consumed != NULL)
    *out_consumed = 0;
  if (scanner == NULL || !scanner->active || out_consumed == NULL ||
      (byte_count != 0 && data == NULL))
    return SALTS_SEARCH_LINES_INVALID;
  if (scanner->closed)
    return SALTS_SEARCH_LINES_CLOSED;
#if SIZE_MAX > UINT64_MAX
  if (byte_count > UINT64_MAX)
    return lines_abort(scanner, SALTS_SEARCH_LINES_OVERFLOW);
#endif
  if ((uint64_t)byte_count > UINT64_MAX - scanner->byte_offset)
    return lines_abort(scanner, SALTS_SEARCH_LINES_OVERFLOW);

  for (i = 0; i < byte_count; ++i) {
    const unsigned char c = data[i];
    salts_search_lines_status rc;
    if (scanner->binary_mode == SALTS_SEARCH_LINES_BINARY_REJECT_NUL && c == 0)
      return lines_abort(scanner, SALTS_SEARCH_LINES_BINARY);
    if (scanner->pending_cr) {
      scanner->pending_cr = false;
      if (c == '\n') {
        ++scanner->byte_offset;
        *out_consumed = i + 1;
        rc = lines_emit(scanner, 2);
        if (rc != SALTS_SEARCH_LINES_OK)
          return rc;
        continue;
      }
      if (scanner->newline_mode == SALTS_SEARCH_LINES_UNIVERSAL) {
        /* The previous CR is a complete delimiter. Current byte is not
         * consumed until the previous line has been published. */
        rc = lines_emit(scanner, 1);
      } else {
        /* A lone CR is content in the default LF/CRLF mode. */
        rc = lines_append(scanner, '\r');
      }
      if (rc != SALTS_SEARCH_LINES_OK)
        return rc;
    }
    ++scanner->byte_offset;
    *out_consumed = i + 1;
    if (c == '\r') {
      scanner->pending_cr = true;
      continue;
    }
    if (c == '\n') {
      rc = lines_emit(scanner, 1);
    } else {
      rc = lines_append(scanner, c);
    }
    if (rc != SALTS_SEARCH_LINES_OK)
      return rc;
  }
  return SALTS_SEARCH_LINES_OK;
}

salts_search_lines_status salts_search_lines_finish(
    salts_search_line_scanner *scanner) {
  salts_search_lines_status rc;
  if (scanner == NULL || !scanner->active)
    return SALTS_SEARCH_LINES_INVALID;
  if (scanner->closed)
    return SALTS_SEARCH_LINES_CLOSED;
  if (scanner->pending_cr) {
    scanner->pending_cr = false;
    if (scanner->newline_mode == SALTS_SEARCH_LINES_UNIVERSAL) {
      rc = lines_emit(scanner, 1);
      if (rc != SALTS_SEARCH_LINES_OK)
        return rc;
    } else {
      rc = lines_append(scanner, '\r');
      if (rc != SALTS_SEARCH_LINES_OK)
        return rc;
    }
  }
  if (scanner->line_size != 0) {
    rc = lines_emit(scanner, 0);
    if (rc != SALTS_SEARCH_LINES_OK)
      return rc;
  }
  scanner->closed = true;
  return SALTS_SEARCH_LINES_OK;
}

#include "search_context.h"
#include <string.h>

static bool context_stop(salts_search_context *ctx,
                         salts_search_context_status reason) {
  ctx->status = reason;
  return false;
}

static bool emit_line(salts_search_context *ctx, const salts_search_line *line,
                      salts_search_context_kind kind,
                      uint64_t relative_start, uint64_t relative_end) {
  salts_search_context_event ev;
  if (ctx->latest_emitted_line &&
      line->line_number <= ctx->latest_emitted_line)
    return true; /* Already emitted by a prior match or after-context. */
  ev.line = *line;
  ev.kind = kind;
  ev.has_match_span = kind == SALTS_SEARCH_CONTEXT_MATCH;
  ev.match_byte_start = 0;
  ev.match_byte_end = 0;
  if (ev.has_match_span) {
    if (relative_start > relative_end || relative_end > line->length ||
        line->byte_start > UINT64_MAX - relative_end)
      return context_stop(ctx, SALTS_SEARCH_CONTEXT_INVALID);
    ev.match_byte_start = line->byte_start + relative_start;
    ev.match_byte_end = line->byte_start + relative_end;
  }
  ev.group_break = ctx->emitted_lines != 0 &&
                   line->line_number - ctx->latest_emitted_line > 1;
  /* Stop is terminal. The callback is not permitted to reenter this collector. */
  if (!ctx->callback(ctx->user, &ev))
    return context_stop(ctx, SALTS_SEARCH_CONTEXT_STOPPED);
  ctx->latest_emitted_line = line->line_number;
  ++ctx->emitted_lines;
  return true;
}

static salts_search_context_status context_init_common(
    salts_search_context *ctx, const salts_search_literal_pattern *literal,
    const salts_search_matcher *matcher,
    salts_search_context_slot *slots, size_t before_count,
    unsigned char *storage, size_t storage_bytes, size_t max_line_bytes,
    size_t after_count, salts_search_context_callback callback, void *user) {
  size_t i;
  if (!ctx || ctx->initialized || ctx->finished || ctx->literal ||
      ctx->matcher || (literal == NULL) == (matcher == NULL) ||
      (literal && !literal->compiled) || (matcher && !matcher->compiled) ||
      !callback ||
      (!slots && before_count) || (!storage && before_count) ||
      !max_line_bytes || before_count > SIZE_MAX / max_line_bytes ||
      storage_bytes < before_count * max_line_bytes)
    return SALTS_SEARCH_CONTEXT_INVALID;
  *ctx = (salts_search_context){0};
  ctx->literal = literal;
  ctx->matcher = matcher;
  ctx->slots = slots;
  ctx->storage = storage;
  ctx->before_count = before_count;
  ctx->after_count = after_count;
  ctx->max_line_bytes = max_line_bytes;
  ctx->callback = callback;
  ctx->user = user;
  ctx->status = SALTS_SEARCH_CONTEXT_OK;
  ctx->initialized = true;
  for (i = 0; i < before_count; ++i) {
    ctx->slots[i] = (salts_search_context_slot){0};
    ctx->slots[i].bytes = storage + i * max_line_bytes;
  }
  return SALTS_SEARCH_CONTEXT_OK;
}


salts_search_context_status salts_search_context_init(
    salts_search_context *ctx, const salts_search_literal_pattern *literal,
    salts_search_context_slot *slots, size_t before_count,
    unsigned char *storage, size_t storage_bytes, size_t max_line_bytes,
    size_t after_count, salts_search_context_callback callback, void *user) {
  return context_init_common(ctx, literal, NULL, slots, before_count,
                             storage, storage_bytes, max_line_bytes,
                             after_count, callback, user);
}

salts_search_context_status salts_search_context_init_matcher(
    salts_search_context *ctx, const salts_search_matcher *matcher,
    salts_search_context_slot *slots, size_t before_count,
    unsigned char *storage, size_t storage_bytes, size_t max_line_bytes,
    size_t after_count, salts_search_context_callback callback, void *user) {
  return context_init_common(ctx, NULL, matcher, slots, before_count,
                             storage, storage_bytes, max_line_bytes,
                             after_count, callback, user);
}

bool salts_search_context_accept_line(void *raw, const salts_search_line *line) {
  salts_search_context *ctx = (salts_search_context *)raw;
  bool matching;
  uint64_t match_relative_start = 0;
  uint64_t match_relative_end = 0;
  size_t i;
  if (!ctx || !ctx->initialized || ctx->finished ||
      ctx->status != SALTS_SEARCH_CONTEXT_OK)
    return false;
  if (!line || !line->line_number ||
      (line->length && !line->bytes) ||
      line->byte_start > line->byte_end ||
      line->length != line->byte_end - line->byte_start ||
      line->terminator_size > 2 ||
      (ctx->latest_input_line &&
       (ctx->latest_input_line == UINT64_MAX ||
        line->line_number != ctx->latest_input_line + 1)))
    return context_stop(ctx, SALTS_SEARCH_CONTEXT_INVALID);
  if (ctx->before_count && line->length > ctx->max_line_bytes)
    return context_stop(ctx, SALTS_SEARCH_CONTEXT_LIMIT);
  if (ctx->matcher) {
    salts_search_matcher_span match = {0};
    const salts_search_matcher_status found = salts_search_matcher_find_first(
        ctx->matcher, line->bytes, line->length, &match);
    if (found == SALTS_SEARCH_MATCHER_LIMIT)
      return context_stop(ctx, SALTS_SEARCH_CONTEXT_LIMIT);
    if (found == SALTS_SEARCH_MATCHER_NO_MEMORY)
      return context_stop(ctx, SALTS_SEARCH_CONTEXT_NO_MEMORY);
    if (found == SALTS_SEARCH_MATCHER_ENGINE_ERROR)
      return context_stop(ctx, SALTS_SEARCH_CONTEXT_MATCH_ERROR);
    if (found != SALTS_SEARCH_MATCHER_NOT_FOUND &&
        found != SALTS_SEARCH_MATCHER_OK)
      return context_stop(ctx, SALTS_SEARCH_CONTEXT_INVALID);
    matching = found == SALTS_SEARCH_MATCHER_OK;
    if (matching) {
      match_relative_start = match.byte_start;
      match_relative_end = match.byte_end;
    }
  } else {
    salts_search_literal_span first = {0};
    const salts_search_literal_status found = salts_search_literal_find(
        ctx->literal, line->bytes, line->length, 0, &first);
    if (found != SALTS_SEARCH_LITERAL_NOT_FOUND &&
        found != SALTS_SEARCH_LITERAL_OK)
      return context_stop(ctx, SALTS_SEARCH_CONTEXT_INVALID);
    matching = found == SALTS_SEARCH_LITERAL_OK;
    if (matching) {
      match_relative_start = (uint64_t)first.start;
      match_relative_end = (uint64_t)first.end;
    }
  }
  if (matching) {
    const size_t oldest = ctx->stored == ctx->before_count ?
                          ctx->next_slot : 0;
    for (i = 0; i < ctx->stored; ++i) {
      const salts_search_context_slot *slot =
          &ctx->slots[(oldest + i) % ctx->before_count];
      const salts_search_line prev = {slot->bytes, slot->length, slot->start,
                                     slot->end, slot->number,
                                     slot->terminator_size};
      if (!emit_line(ctx, &prev, SALTS_SEARCH_CONTEXT_BEFORE, 0, 0))
        return false;
    }
    ++ctx->match_lines;
    if (!emit_line(ctx, line, SALTS_SEARCH_CONTEXT_MATCH,
                   match_relative_start, match_relative_end))
      return false;
    ctx->after_remaining = ctx->after_count;
  } else if (ctx->after_remaining) {
    if (!emit_line(ctx, line, SALTS_SEARCH_CONTEXT_AFTER, 0, 0))
      return false;
    --ctx->after_remaining;
  }
  if (ctx->before_count) {
    salts_search_context_slot *slot = &ctx->slots[ctx->next_slot];
    if (line->length)
      memcpy(slot->bytes, line->bytes, line->length);
    slot->length = line->length;
    slot->start = line->byte_start;
    slot->end = line->byte_end;
    slot->number = line->line_number;
    slot->terminator_size = line->terminator_size;
    ctx->next_slot = ctx->next_slot + 1 == ctx->before_count ?
                     0 : ctx->next_slot + 1;
    if (ctx->stored < ctx->before_count)
      ++ctx->stored;
  }
  ctx->latest_input_line = line->line_number;
  return true;
}

salts_search_context_status salts_search_context_finish(salts_search_context *ctx) {
  if (!ctx || !ctx->initialized)
    return SALTS_SEARCH_CONTEXT_INVALID;
  if (ctx->status != SALTS_SEARCH_CONTEXT_OK)
    return ctx->status;
  if (ctx->finished)
    return SALTS_SEARCH_CONTEXT_CLOSED;
  ctx->finished = true;
  return SALTS_SEARCH_CONTEXT_OK;
}

/* Query owner to per-hit callback relay. Input slices and event are borrowed
 * until the client callback returns; no per-hit dynamic allocation. */
typedef struct salts_search_hit_relay {
  salts_search_hit_stream *stream;
  const salts_search_line *line;
} salts_search_hit_relay;

static bool search_hit_stop(salts_search_hit_stream *stream,
                            salts_search_context_status reason) {
  if (stream->status == SALTS_SEARCH_CONTEXT_OK)
    stream->status = reason;
  stream->budget.terminal = true;
  return false;
}

static bool search_hit_forward(void *raw,
                               const salts_search_matcher_span *span) {
  salts_search_hit_relay *relay = (salts_search_hit_relay *)raw;
  salts_search_hit_stream *stream = relay->stream;
  const salts_search_line *line = relay->line;
  salts_search_hit_event event;
  bool keep_going;
  if (stream->status != SALTS_SEARCH_CONTEXT_OK)
    return false;
  if (span->byte_start > span->byte_end ||
      span->byte_end > (uint64_t)line->length ||
      line->byte_start > UINT64_MAX - span->byte_end)
    return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_INVALID);
  event.line = *line;
  event.match_byte_start = line->byte_start + span->byte_start;
  event.match_byte_end = line->byte_start + span->byte_end;
  keep_going = stream->callback(stream->user, &event);
  /* A callback may attempt reentrant finish/accept and ignore its error.
   * Such an attempt must still terminally poison the outer enumeration. */
  if (stream->status != SALTS_SEARCH_CONTEXT_OK)
    return false;
  if (!keep_going)
    return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_STOPPED);
  return true;
}

salts_search_context_status salts_search_hit_stream_init(
    salts_search_hit_stream *stream, const salts_search_matcher *matcher,
    size_t max_hits, size_t max_scans, bool overlapping,
    salts_search_hit_callback callback, void *user) {
  if (!stream || !callback || stream->initialized || stream->finished ||
      stream->budget.initialized || stream->callback != NULL ||
      !matcher || !matcher->compiled)
    return SALTS_SEARCH_CONTEXT_INVALID;
  if (salts_search_matcher_query_budget_init(
          &stream->budget, matcher, max_hits, max_scans) !=
      SALTS_SEARCH_MATCHER_OK)
    return SALTS_SEARCH_CONTEXT_INVALID;
  stream->callback = callback;
  stream->user = user;
  stream->overlapping = overlapping;
  stream->status = SALTS_SEARCH_CONTEXT_OK;
  stream->initialized = true;
  return SALTS_SEARCH_CONTEXT_OK;
}

bool salts_search_hit_stream_accept_line(void *raw,
                                          const salts_search_line *line) {
  salts_search_hit_stream *stream = (salts_search_hit_stream *)raw;
  salts_search_hit_relay relay;
  salts_search_matcher_status rc;
  size_t emitted = 0;
  if (!stream)
    return false;
  if (stream->in_line)
    return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_INVALID);
  if (!stream->initialized || stream->finished ||
      stream->status != SALTS_SEARCH_CONTEXT_OK)
    return false;
#if SIZE_MAX > UINT64_MAX
  if (line && line->length > UINT64_MAX)
    return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_INVALID);
#endif
  if (!line || !line->line_number ||
      (line->length && !line->bytes) ||
      line->byte_start > line->byte_end ||
      (uint64_t)line->length != line->byte_end - line->byte_start ||
      line->terminator_size > 2u ||
      line->byte_end > UINT64_MAX - (uint64_t)line->terminator_size)
    return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_INVALID);
  if (stream->have_last_line &&
      (!stream->last_terminated ||
       stream->last_line_number == UINT64_MAX ||
       line->line_number != stream->last_line_number + 1u ||
       line->byte_start != stream->next_byte_start))
    return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_INVALID);

  relay.stream = stream;
  relay.line = line;
  stream->in_line = true;
  rc = salts_search_matcher_query_enumerate(
      &stream->budget, line->bytes, line->length, stream->overlapping,
      search_hit_forward, &relay, &emitted);
  stream->in_line = false;
  if (emitted)
    ++stream->matched_lines; /* includes lines with terminal partial hits */
  if (stream->status != SALTS_SEARCH_CONTEXT_OK)
    return false;
  switch (rc) {
    case SALTS_SEARCH_MATCHER_OK:
      stream->last_line_number = line->line_number;
      stream->next_byte_start =
          line->byte_end + (uint64_t)line->terminator_size;
      stream->last_terminated = line->terminator_size != 0u;
      stream->have_last_line = true;
      return true;
    case SALTS_SEARCH_MATCHER_LIMIT:
      return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_LIMIT);
    case SALTS_SEARCH_MATCHER_STOPPED:
      return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_STOPPED);
    case SALTS_SEARCH_MATCHER_NO_MEMORY:
      return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_NO_MEMORY);
    case SALTS_SEARCH_MATCHER_ENGINE_ERROR:
      return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_MATCH_ERROR);
    default:
      return search_hit_stop(stream, SALTS_SEARCH_CONTEXT_INVALID);
  }
}

salts_search_context_status salts_search_hit_stream_finish(
    salts_search_hit_stream *stream) {
  if (!stream || !stream->initialized)
    return SALTS_SEARCH_CONTEXT_INVALID;
  if (stream->in_line) {
    search_hit_stop(stream, SALTS_SEARCH_CONTEXT_INVALID);
    return stream->status;
  }
  if (stream->status != SALTS_SEARCH_CONTEXT_OK)
    return stream->status;
  if (stream->finished)
    return SALTS_SEARCH_CONTEXT_CLOSED;
  stream->finished = true;
  return SALTS_SEARCH_CONTEXT_OK;
}

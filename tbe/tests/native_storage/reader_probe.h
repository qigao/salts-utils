#ifndef DATA_BIND_TEST_READER_PROBE_H
#define DATA_BIND_TEST_READER_PROBE_H

/* #99 test input only: no decoder, metadata, owner hold, or implicit framing.
 * Steps and stable slices must outlive every read. A transient slice is valid
 * only until the next source callback, including an error or end-of-input. */
#include <cserde/reader.h>
#include <string.h>

enum {
  NATIVE_READER_PROBE_CAPACITY = 64,
  NATIVE_READER_PROBE_POISON = 0xdd
};

typedef struct NativeReaderProbeStep {
  cserde_status status;
  cserde_token token;
} NativeReaderProbeStep;

typedef struct NativeReaderProbe {
  const NativeReaderProbeStep *steps;
  size_t step_count;
  size_t position;
  size_t calls;
  size_t delivered;
  size_t invalidations;
  unsigned char transient[NATIVE_READER_PROBE_CAPACITY];
} NativeReaderProbe;

static inline NativeReaderProbeStep native_reader_probe_token(cserde_token_kind kind) {
  NativeReaderProbeStep step;
  memset(&step, 0, sizeof(step));
  step.status = CSERDE_OK;
  step.token.kind = kind;
  return step;
}

static inline NativeReaderProbeStep native_reader_probe_sint(int64_t value) {
  NativeReaderProbeStep step = native_reader_probe_token(CSERDE_SINT);
  step.token.value.sint = value;
  return step;
}

static inline NativeReaderProbeStep native_reader_probe_slice(
    cserde_token_kind kind, const unsigned char *data, size_t size,
    cserde_view_lifetime lifetime) {
  NativeReaderProbeStep step = native_reader_probe_token(kind);
  step.token.value.slice.data = data;
  step.token.value.slice.size = size;
  step.token.value.slice.lifetime = lifetime;
  return step;
}

static inline NativeReaderProbeStep native_reader_probe_error(cserde_status status) {
  NativeReaderProbeStep step = native_reader_probe_token(CSERDE_NULL);
  step.status = status;
  return step;
}

static inline cserde_status native_reader_probe_next(void *context, cserde_token *out) {
  NativeReaderProbe *probe = (NativeReaderProbe *)context;
  const NativeReaderProbeStep *step;
  if (probe == NULL || out == NULL) return CSERDE_SOURCE_ERROR;
  ++probe->calls;
  ++probe->invalidations;
  memset(probe->transient, NATIVE_READER_PROBE_POISON, sizeof(probe->transient));
  if (probe->position == probe->step_count) return CSERDE_DONE;
  step = &probe->steps[probe->position++];
  if (step->status != CSERDE_OK) return step->status;
  *out = step->token;
  if ((out->kind == CSERDE_STRING || out->kind == CSERDE_BYTES) &&
      out->value.slice.lifetime == CSERDE_VIEW_TRANSIENT &&
      out->value.slice.data != NULL) {
    /* This is fixture capacity, never a DataBind resource-limit result. */
    if (out->value.slice.size > sizeof(probe->transient))
      return CSERDE_LIMIT_EXCEEDED;
    memcpy(probe->transient, out->value.slice.data, out->value.slice.size);
    out->value.slice.data = probe->transient;
  }
  ++probe->delivered;
  return CSERDE_OK;
}

static inline cserde_status native_reader_probe_open(
    NativeReaderProbe *probe, const NativeReaderProbeStep *steps,
    size_t step_count, cserde_reader *reader) {
  static const cserde_reader_ops ops = {
      sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION,
      native_reader_probe_next};
  NativeReaderProbe initialized;
  cserde_status status;
  if (probe == NULL || reader == NULL || (step_count != 0u && steps == NULL))
    return CSERDE_INVALID_ARGUMENT;
  memset(&initialized, 0, sizeof(initialized));
  initialized.steps = steps;
  initialized.step_count = step_count;
  /* Let the real reader reject reuse of a live/failed reader. Do not reset it. */
  status = cserde_reader_init(reader, &ops, probe);
  if (status == CSERDE_OK) *probe = initialized;
  return status;
}

#endif /* DATA_BIND_TEST_READER_PROBE_H */

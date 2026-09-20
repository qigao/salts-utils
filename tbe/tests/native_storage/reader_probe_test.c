/* Independent source controls for #99. Passing these tests does not prove
 * DataBind decoding, publication, rollback, or owned-result lifetime. */
#include "reader_probe.h"
#include <tinytest.h>

static NativeReaderProbe probe;
static cserde_reader reader;
static cserde_token token;

spec("DataBind direct-reader observable source controls") {
  before_each() {
    memset(&probe, 0, sizeof(probe));
    memset(&reader, 0, sizeof(reader));
    memset(&token, 0, sizeof(token));
  }

  it("opens without reading or delivering an input token") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    check_equal(native_reader_probe_open(&probe, steps, 1u, &reader), CSERDE_OK);
    check_equal(probe.calls, 0u);
    check_equal(probe.position, 0u);
    check_equal(probe.delivered, 0u);
    check_equal(reader.state, CSERDE_READER_READY);
  }

  it("counts the extra EOF read after two separately observable root values") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_sint(7), native_reader_probe_sint(-9)};
    check_equal(native_reader_probe_open(&probe, steps, 2u, &reader), CSERDE_OK);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
    check_equal(token.value.sint, INT64_C(7));
    check_equal(probe.calls, 1u);
    check_equal(probe.position, 1u);
    check_equal(reader.state, CSERDE_READER_READY);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
    check_equal(token.value.sint, -INT64_C(9));
    check_equal(probe.calls, 2u);
    check_equal(probe.delivered, 2u);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_DONE);
    check_equal(probe.calls, 3u);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_DONE);
    check_equal(probe.calls, 3u);
  }

  it("reuses transient storage while a caller copy retains embedded NUL bytes") {
    static const unsigned char first[] = {'A', 0u, 'B'};
    static const unsigned char second[] = {'x', 'y', 'z'};
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_slice(CSERDE_STRING, first, sizeof(first), CSERDE_VIEW_TRANSIENT),
        native_reader_probe_slice(CSERDE_BYTES, second, sizeof(second), CSERDE_VIEW_TRANSIENT)};
    const unsigned char *borrow;
    unsigned char copy[sizeof(first)];
    check_equal(native_reader_probe_open(&probe, steps, 2u, &reader), CSERDE_OK);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
    borrow = token.value.slice.data;
    check_true(borrow == probe.transient);
    check_equal(token.value.slice.size, sizeof(first));
    memcpy(copy, borrow, sizeof(copy));
    check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
    check_true(token.value.slice.data == borrow);
    check_equal(memcmp(borrow, second, sizeof(second)), 0);
    check_equal(memcmp(copy, first, sizeof(first)), 0);
    check_equal(probe.invalidations, 2u);
  }

  it("invalidates a transient view even when the next callback returns DONE") {
    static const unsigned char bytes[] = {'a', 'b'};
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_slice(CSERDE_BYTES, bytes, sizeof(bytes), CSERDE_VIEW_TRANSIENT)};
    const unsigned char *borrow;
    check_equal(native_reader_probe_open(&probe, steps, 1u, &reader), CSERDE_OK);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
    borrow = token.value.slice.data;
    check_equal(cserde_reader_next(&reader, &token), CSERDE_DONE);
    check_equal(borrow[0], NATIVE_READER_PROBE_POISON);
    check_equal(borrow[1], NATIVE_READER_PROBE_POISON);
    check_equal(probe.calls, 2u);
  }

  it("does not copy or invalidate a source-owned stable slice") {
    static const unsigned char bytes[] = {'s', 0u, 't'};
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_slice(CSERDE_BYTES, bytes, sizeof(bytes), CSERDE_VIEW_STABLE)};
    check_equal(native_reader_probe_open(&probe, steps, 1u, &reader), CSERDE_OK);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
    check_true(token.value.slice.data == bytes);
    check_equal(token.value.slice.lifetime, CSERDE_VIEW_STABLE);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_DONE);
    check_equal(memcmp(token.value.slice.data, bytes, sizeof(bytes)), 0);
  }

  it("injects a source error without replaying the remaining script") {
    static const unsigned char bytes[] = {'e'};
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_slice(CSERDE_BYTES, bytes, sizeof(bytes), CSERDE_VIEW_TRANSIENT),
        native_reader_probe_error(CSERDE_SOURCE_ERROR), native_reader_probe_sint(42)};
    const unsigned char *borrow;
    check_equal(native_reader_probe_open(&probe, steps, 3u, &reader), CSERDE_OK);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
    borrow = token.value.slice.data;
    check_equal(cserde_reader_next(&reader, &token), CSERDE_SOURCE_ERROR);
    check_equal(borrow[0], NATIVE_READER_PROBE_POISON);
    check_equal(probe.calls, 2u);
    check_equal(probe.position, 2u);
    check_equal(probe.delivered, 1u);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_SOURCE_ERROR);
    check_equal(probe.calls, 2u);
    check_equal(probe.position, 2u);
  }

  it("lets the real reader reject malformed tokens rather than repairing them") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_slice(CSERDE_BYTES, NULL, 1u, CSERDE_VIEW_TRANSIENT)};
    check_equal(native_reader_probe_open(&probe, steps, 1u, &reader), CSERDE_OK);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_INVALID_TOKEN);
    check_equal(probe.calls, 1u);
    check_equal(reader.state, CSERDE_READER_FAILED);
  }

  it("leaves a truncated map truncated for the real CSerde value traversal") {
    static const unsigned char key[] = {'v'};
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN),
        native_reader_probe_slice(CSERDE_STRING, key, sizeof(key), CSERDE_VIEW_TRANSIENT)};
    check_equal(native_reader_probe_open(&probe, steps, 2u, &reader), CSERDE_OK);
    check_equal(cserde_reader_skip_value(&reader, 1u), CSERDE_UNEXPECTED_END);
    check_equal(probe.calls, 3u);
    check_equal(probe.position, 2u);
    check_equal(probe.delivered, 2u);
  }

  it("rejects fixture overflow before copying and does not silently reopen a failed reader") {
    static const unsigned char too_large[NATIVE_READER_PROBE_CAPACITY + 1u] = {0};
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_slice(CSERDE_BYTES, too_large, sizeof(too_large), CSERDE_VIEW_TRANSIENT)};
    cserde_reader fresh = {0};
    check_equal(native_reader_probe_open(&probe, steps, 1u, &reader), CSERDE_OK);
    check_equal(cserde_reader_next(&reader, &token), CSERDE_LIMIT_EXCEEDED);
    check_equal(probe.calls, 1u);
    check_equal(probe.delivered, 0u);
    check_equal(native_reader_probe_open(&probe, steps, 1u, &reader), CSERDE_INVALID_STATE);
    check_equal(probe.calls, 1u);
    check_equal(probe.position, 1u);
    /* Reuse is explicit, with a new zero-state reader; never hidden replay. */
    check_equal(native_reader_probe_open(&probe, NULL, 0u, &fresh), CSERDE_OK);
    check_equal(probe.calls, 0u);
    check_equal(cserde_reader_next(&fresh, &token), CSERDE_DONE);
    check_equal(probe.calls, 1u);
  }
}

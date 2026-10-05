#ifndef DATA_BIND_BINARY_READER_H
#define DATA_BIND_BINARY_READER_H

#include "data_bind_binary_layout.h"

#include <cserde/reader.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open one borrowed Binary payload as a canonical CSerde message reader.
 *
 * The provider borrows payload bytes and allocates only one fixed-size reader
 * lease object; it never copies payload data or performs schema/reflection
 * lookup. Before publishing the reader, the complete GROUP/VAR_DATA tail is checked
 * for length-prefix/payload bounds, recursive active-record state consistency
 * and trailing bytes. Fixed child records emit nested MAP tokens; GROUP emits
 * ARRAY of MAPs, skipping extended wire strides without copying entries.
 * Fixed arrays borrow an exact inline span and emit the declared number of
 * scalar/bytes/MAP elements, without consuming a count header.
 * COUNTED list/set/map entries consume their u32 count in declaration order;
 * CURSOR_FIXED fields consume exact extents after those entries. Counted maps
 * emit MAP tokens with borrowed length-prefixed STRING keys. Payload bytes and
 * per-collection counts are bounded by DATA_BIND_BINARY_LAYOUT_MAX_PAYLOAD_BYTES
 * and MAX_ITEMS (library build definitions); exhaustion returns LIMIT.
 * Access is single-threaded; payload and all reachable plan storage stay alive until close.
 * max_depth bounds MAP/ARRAY depth including the root; zero selects MAX_DEPTH.
 * A value above MAX_DEPTH returns INVALID_ARG without opening a lease.
 * Invalid layouts return SCHEMA, excess depth LIMIT, and malformed wire PARSE;
 * failures leave both output handles NULL.
 */
DATA_BIND_API DataBindStatus data_bind_binary_reader_open(
    const DataBindBinaryLayoutPlan *plan,
    const void *payload,
    size_t payload_bytes,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error);

DATA_BIND_API void data_bind_binary_reader_close(
    cserde_reader *reader,
    void *owner);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_BINARY_READER_H */

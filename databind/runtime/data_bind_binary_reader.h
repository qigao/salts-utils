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
 * lookup. Before publishing the reader, the complete VAR_DATA tail is checked
 * for length-prefix/payload bounds, recursive active-record state consistency
 * and trailing bytes. Fixed child records emit nested MAP tokens. Access is
 * single-threaded; payload and all reachable plan storage stay alive until close.
 * max_depth bounds record MAP depth including the root; zero selects MAX_DEPTH.
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

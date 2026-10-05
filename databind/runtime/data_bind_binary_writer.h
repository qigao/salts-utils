#ifndef DATA_BIND_BINARY_WRITER_H
#define DATA_BIND_BINARY_WRITER_H

#include "data_bind_binary_layout.h"

#include <cserde/writer.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open one schema-specific Binary layout as a canonical CSerde writer.
 *
 * The writer buffers one complete Binary message, validates canonical field
 * order/state against the immutable layout plan, and calls the byte sink once
 * on finish. This preserves all-or-nothing publication for bounded generated
 * sinks such as *_to_bin_into.
 * Fixed child MAPs write into their exact ranges in the same buffer, without
 * additional allocations. Access is single-threaded; all reachable plan storage
 * stays immutable and alive until close. max_depth bounds active record MAPs
 * including the root; zero selects MAX_DEPTH. Schema/token errors and depth
 * exhaustion prevent publication, and close releases the complete writer lease.
 */
DATA_BIND_API DataBindStatus data_bind_binary_writer_open(
    const DataBindBinaryLayoutPlan *plan,
    DataBindWriteFn write,
    void *write_user,
    size_t max_depth,
    cserde_writer **out_writer,
    void **out_owner,
    DataBindError *error);

DATA_BIND_API DataBindStatus data_bind_binary_writer_close(
    cserde_writer *writer,
    void *owner,
    DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_BINARY_WRITER_H */

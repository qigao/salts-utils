#ifndef DATA_BIND_VALUE_READER_INTERNAL_H
#define DATA_BIND_VALUE_READER_INTERNAL_H

#include "data_bind_value_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* @internal #489: borrow a schema-bound value tree under the public reader's
 * lifetime and budget protocol. Canonical CMeta builtin identities determine
 * integer token signedness and range, independently of DataBindValue storage.
 * Domain integers (including enums) retain their exact numeric tokens; their
 * Contract/MessagePlan still owns domain validation. No wire facts are read.
 * The public reader keeps its existing storage-based token behavior. */
DATA_BIND_API DataBindStatus data_bind_internal_value_reader_open_typed(
    const DataBindValue *value, const DataBindValueReaderLimits *limits,
    cserde_reader **out_reader, DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif

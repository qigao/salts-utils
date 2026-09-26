#ifndef DATA_BIND_MESSAGE_EXECUTOR_H
#define DATA_BIND_MESSAGE_EXECUTOR_H

#include "data_bind_message_plan.h"
#include "data_bind_native.h"

#include <cserde/cserde.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_MESSAGE_EXECUTOR_ABI_VERSION = 1u };

typedef struct DataBindMessageDecodeRequirements {
  size_t size;
  uint32_t abi_version;

  size_t destination_bytes;
  size_t field_tracking_bytes;
  size_t native_decode_bytes;
  size_t workspace_alignment;
  size_t workspace_bytes;
} DataBindMessageDecodeRequirements;

#define DATA_BIND_MESSAGE_DECODE_REQUIREMENTS_INIT \
  { sizeof(DataBindMessageDecodeRequirements), \
    DATA_BIND_MESSAGE_EXECUTOR_ABI_VERSION, 0u, 0u, 0u, 0u, 0u }

typedef struct DataBindMessageDecodeDiagnostic {
  size_t size;
  uint32_t abi_version;
  DataBindError error;
  cserde_status source_status;
} DataBindMessageDecodeDiagnostic;

#define DATA_BIND_MESSAGE_DECODE_DIAGNOSTIC_INIT \
  { sizeof(DataBindMessageDecodeDiagnostic), \
    DATA_BIND_MESSAGE_EXECUTOR_ABI_VERSION, DATA_BIND_ERROR_INIT, CSERDE_OK }

/**
 * Measure the caller workspace required by whole-message execution.
 *
 * native_options supplies the same global max_depth/max_items/max_owned_bytes
 * policy used by decode. Its workspace is borrowed only as native measurement
 * probe scratch and retained nowhere. requirements is published only on
 * success.
 *
 * workspace_bytes includes:
 * - one field-seen bitmap;
 * - worst-case alignment padding;
 * - the canonical root native decode requirement, which is an upper bound for
 *   each exact field decode performed by MessagePlan.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_measure_decode(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    DataBindMessageDecodeRequirements *requirements,
    DataBindMessageDecodeDiagnostic *diagnostic);

/**
 * Decode exactly one CSerde map into one generated native DataBind record.
 *
 * destination is staging storage, not an existing live value. The executor
 * initializes the canonical CMeta value graph and DataBind state bits itself.
 * On any parse/type/default/validation/provider failure, initialized native
 * state is restored and all compiled presence/null bits are cleared. On
 * success, destination owns one complete live native value and the caller must
 * release it through the canonical native lifecycle.
 *
 * native_options budgets apply to the whole logical message, not per field.
 * The root message itself consumes one semantic item. Per-field native decodes
 * carry cumulative item/owned-byte usage through the internal native seam.
 *
 * max_buffer_bytes is an additional per-owned-value limit and does not relax
 * aggregate max_owned_bytes.
 *
 * The reader remains caller-owned. This function consumes exactly one map and
 * does not probe for a following value or EOF.
 */
DATA_BIND_API DataBindStatus data_bind_message_plan_decode(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    size_t max_buffer_bytes,
    DataBindMessageDecodeDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_MESSAGE_EXECUTOR_H */

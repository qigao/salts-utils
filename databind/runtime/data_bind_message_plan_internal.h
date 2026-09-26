#ifndef DATA_BIND_MESSAGE_PLAN_INTERNAL_H
#define DATA_BIND_MESSAGE_PLAN_INTERNAL_H

#include "data_bind_message_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validate one already-normalized VALUE field by canonical field identity.
 * Service BindingPlan uses this to preserve its existing per-field validation
 * order while validation ownership moves into MessagePlan.
 */
DataBindStatus data_bind_message_plan_internal_validate_field(
    const DataBindMessagePlan *plan,
    const char *field_name,
    const void *source,
    DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_MESSAGE_PLAN_INTERNAL_H */

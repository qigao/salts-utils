#ifndef DATA_BIND_VALIDATION_PLAN_H
#define DATA_BIND_VALIDATION_PLAN_H

#include "data_bind.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Owned immutable validation artifact compiled from one DataBind record type. */
typedef struct DataBindValidationPlan DataBindValidationPlan;

/**
 * Compile normalized reflected constraints into one owned immutable plan.
 *
 * This is the only schema/reflection phase. The plan copies every field name
 * and bound needed for execution and remains valid after the codec is freed.
 *
 * On failure, *out_plan is set to NULL.
 */
DATA_BIND_API DataBindStatus data_bind_validation_plan_compile(
    DataBind *codec, const char *type_name,
    DataBindValidationPlan **out_plan, DataBindError *error);

/** Release an immutable plan. NULL is allowed. */
DATA_BIND_API void data_bind_validation_plan_free(
    DataBindValidationPlan *plan);

/** Return the canonical type name copied into the plan, or NULL. */
DATA_BIND_API const char *data_bind_validation_plan_type_name(
    const DataBindValidationPlan *plan);

/** Return the number of compiled direct-field rules. */
DATA_BIND_API size_t data_bind_validation_plan_rule_count(
    const DataBindValidationPlan *plan);

/**
 * Validate one already decoded/bound object.
 *
 * Presence/default/nullability normalization must have completed before this
 * call. Missing and explicit NULL fields are skipped by value constraints.
 * Execution performs no schema/reflection lookup.
 */
DATA_BIND_API DataBindStatus data_bind_validation_plan_validate(
    const DataBindValidationPlan *plan, const DataBindValue *value,
    DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_VALIDATION_PLAN_H */

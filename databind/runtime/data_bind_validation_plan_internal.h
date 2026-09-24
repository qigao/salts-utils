#ifndef DATA_BIND_VALIDATION_PLAN_INTERNAL_H
#define DATA_BIND_VALIDATION_PLAN_INTERNAL_H

#include "data_bind_validation_plan.h"

#include <cmeta/data.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DataBindValidationRuleInfo {
  const char *field_name;
  DataBindSchemaConstraintKind kind;
  cmeta_data_kind field_kind;
} DataBindValidationRuleInfo;

size_t data_bind_validation_plan_internal_child_count(
    const DataBindValidationPlan *plan);

int data_bind_validation_plan_internal_rule_info(
    const DataBindValidationPlan *plan, size_t rule_index,
    DataBindValidationRuleInfo *out);

/*
 * Execute one already-compiled direct rule against an already-admitted native
 * leaf. No schema/reflection lookup or constraint parsing occurs here.
 */
DataBindStatus data_bind_validation_plan_internal_validate_native_rule(
    const DataBindValidationPlan *plan, size_t rule_index,
    const cmeta_data_desc *data, const void *source, DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_VALIDATION_PLAN_INTERNAL_H */

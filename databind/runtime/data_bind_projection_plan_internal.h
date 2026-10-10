#ifndef DATA_BIND_PROJECTION_PLAN_INTERNAL_H
#define DATA_BIND_PROJECTION_PLAN_INTERNAL_H

#include "data_bind_projection_plan.h"

/* Borrowed immutable XML projection facts. UINT32_MAX denotes a scalar child;
 * SIZE_MAX denotes an unknown field. No schema/codec is retained by a plan. */
typedef struct DataBindXmlFieldPlan {
  const char *name;
  uint32_t child;
  int sequence;
} DataBindXmlFieldPlan;

int data_bind_format_plan_xml_record(const DataBindFormatPlan *plan);
int data_bind_format_plan_has_sequences(const DataBindFormatPlan *plan);
size_t data_bind_format_record_field_count(const DataBindFormatPlan *plan, uint32_t record);
int data_bind_format_record_field_at(const DataBindFormatPlan *plan, uint32_t record,
                                    size_t index, DataBindXmlFieldPlan *out);
size_t data_bind_format_record_field_find(const DataBindFormatPlan *plan, uint32_t record,
                                        const char *name, size_t length);

#endif

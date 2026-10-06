#ifndef DATABIND_BINARY_LAYOUT_LOWERING_H
#define DATABIND_BINARY_LAYOUT_LOWERING_H

#include "binary_layout_ir.h"
#include "../../../runtime/data_bind_binary_layout.h"

enum {
  DATABIND_BINARY_LOWERING_MAX_TYPES = DATABIND_BINARY_FORMAT_MAX_TYPES,
  DATABIND_BINARY_LOWERING_MAX_FIELDS = DATABIND_BINARY_FORMAT_MAX_FIELDS
};

typedef struct databind_binary_execution_graph databind_binary_execution_graph;

/* Control-plane lowering owns a bounded immutable graph. Contract/format input
 * is borrowed only during build; published plan strings/tables belong to the
 * graph. Close every reader/writer before destroying it. Failure leaves *out
 * NULL and releases every partially built node. No native offsets or lifecycle
 * metadata are consulted. Single-threaded build/destruction; immutable borrow
 * permits independent readers while the owner remains alive. */
databind_binary_layout_status databind_binary_execution_graph_build(
    const IdlContract *contract, const databind_binary_format_plan *format_plan,
    const char *type_name, databind_binary_execution_graph **out,
    databind_binary_layout_diagnostic *diagnostic);

const DataBindBinaryLayoutPlan *databind_binary_execution_graph_root(
    const databind_binary_execution_graph *graph);
void databind_binary_execution_graph_destroy(databind_binary_execution_graph *graph);

/* Pure canonical child resolution used by graph lowering and static emission. */
const char *databind_binary_execution_child_type(
    const IdlContract *contract, const char *type_name,
    const databind_binary_field_layout *field);

#endif

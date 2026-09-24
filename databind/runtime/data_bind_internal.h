#ifndef DATA_BIND_INTERNAL_H
#define DATA_BIND_INTERNAL_H

#include "data_bind.h"
#include "data_bind_value_internal.h"
#include "node_tree.h"
#include "fmt.h"
#include <csv_parser.h>
#include <json_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrows json for the duration of the call; the returned object owns its bound value tree. */
DataBindStatus data_bind_object_from_json_value(DataBind *codec, const char *type_name,
                                                json_value_t *json,
                                                DataBindObject **out_object,
                                                DataBindError *error);

/* Borrows the schema and JSON object; applies the schema's input name/aliases. */
json_value_t *data_bind_internal_json_field_value(
    DataBind *codec, const char *type_name, size_t field_index,
    const json_value_t *object);
const char *data_bind_internal_json_field_output_name(
    DataBind *codec, const char *type_name, size_t field_index);
Node *data_bind_internal_schema_field_node(
    DataBind *codec, const char *type_name, size_t field_index);
size_t data_bind_internal_field_input_name_count(
    DataBind *codec, const char *type_name, size_t field_index);
const char *data_bind_internal_field_input_name_at(
    DataBind *codec, const char *type_name, size_t field_index,
    size_t input_name_index);
int data_bind_internal_csv_find_path_column(
    const csv_doc_t *document, const char *path, size_t *out_column);
int data_bind_internal_csv_header_matches_path(const char *header,
                                               const char *path);

/* Shared text-number grammar for schema integers; does not allocate. */
int data_bind_internal_parse_integer_magnitude(
    const char *text, size_t len, uint64_t max_value, int allow_negative,
    uint64_t *out, int *negative);

/* Test-only seam: advances container generation without changing its contents. */
DATA_BIND_API DataBindStatus
data_bind_internal_test_touch_generation(DataBindValue *value);

/* Test-only seam: fail graph allocation after the requested successful calls.
 * Pass SIZE_MAX to restore normal allocation. */
DATA_BIND_API DataBindStatus
data_bind_internal_test_set_dynamic_graph_allocation_failure(
    size_t successful_allocations);

#ifdef __cplusplus
}
#endif

#endif

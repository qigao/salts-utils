#include "data_bind_projection_plan_internal.h"
#include "data_bind_internal.h"

#include <vstr.h>
#include <cstl/typed.h>
#include <cmeta_cmeta_data.h>
#include <cmeta/data_reflect.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DATA_BIND_PLAN_MAX_TYPES = 64u };

/* Layout alone cannot infer owning tstr semantics from its char * storage. */
typedef struct DataBindFormatNameMap {
  tstr external_name;
  tstr canonical_name;
  uint32_t child;
  uint32_t sequence;
} DataBindFormatNameMap;

#define PLAN_NAME_STABLE_ID "salts-utils.databind.format-name"
cmeta_reflect_value(DataBindFormatNameMap, PLAN_NAME_STABLE_ID,
    cmeta_data_field_id(tstr, external_name, PLAN_NAME_STABLE_ID ".external",
        SALTS_TSTR_CMETA_DATA_REF, SALTS_TSTR_CMETA_TYPE_REF)
    cmeta_data_field_id(tstr, canonical_name, PLAN_NAME_STABLE_ID ".canonical",
        SALTS_TSTR_CMETA_DATA_REF, SALTS_TSTR_CMETA_TYPE_REF)
    cmeta_data_field_id(uint32_t, child, PLAN_NAME_STABLE_ID ".child",
        &cmeta_data_uint32, &cmeta_type_uint32)
    cmeta_data_field_id(uint32_t, sequence, PLAN_NAME_STABLE_ID ".sequence",
        &cmeta_data_uint32, &cmeta_type_uint32)
);
CMETA_DEFINE_DATA_TRAITS(DataBindFormatNameMap, cmeta_reflected_data(DataBindFormatNameMap));
/* CSTL needs a trait-bearing storage descriptor. Its lifecycle delegates to
 * the same reflected value; the bridge preserves the canonical identity. */
static const cmeta_type_identity PLAN_NAME_ID =
    CMETA_TYPE_ID_ATOM_INIT(PLAN_NAME_STABLE_ID);
static const cmeta_type_desc PLAN_NAME_TYPE = {
    "DataBindFormatNameMap", sizeof(DataBindFormatNameMap),
    _Alignof(DataBindFormatNameMap), CMETA_T_OBJECT, NULL,
    &cmeta_traits_DataBindFormatNameMap, &PLAN_NAME_ID};
cmeta_type(Vec, DataBindFormatNames, DataBindFormatNameMap,
           &PLAN_NAME_TYPE, cmeta_reflected_data(DataBindFormatNameMap));
#undef PLAN_NAME_STABLE_ID

typedef struct DataBindFormatRecord {
  DataBindFormatNames names;
  DataBindFormatNames outputs;
} DataBindFormatRecord;

struct DataBindFormatPlan {
  char *type_name;
  DataBindFormat format;
  uint32_t value_states;
  DataBindSchemaKind root_kind;
  int has_optional;
  int has_nullable;
  int has_nested_name_mapping;
  int has_sequences;
  size_t record_count;
  DataBindFormatRecord records[DATA_BIND_PLAN_MAX_TYPES];
};

struct DataBindTransportPlan {
  DataBindTransportKind kind;
  char *service_name;
  char *operation_name;
  DataBindFormatPlan *ingress;
  DataBindFormatPlan *egress;
};

typedef struct DataBindPlanScan {
  const char *visited[DATA_BIND_PLAN_MAX_TYPES];
  size_t visited_count;
  DataBindSchemaKind root_kind;
  int has_optional;
  int has_nullable;
  int has_csv_unsupported_shape;
  const char *csv_unsupported_field;
  int has_xml_unsupported_shape;
  const char *xml_unsupported_field;
  int has_nested_name_mapping;
} DataBindPlanScan;

static size_t plan_out_size(size_t requested, size_t full) {
  return requested != 0u && requested < full ? requested : full;
}

static DataBindStatus plan_error(
    DataBindError *error,
    DataBindStatus status,
    const char *message) {
  size_t size;
  if (error == NULL) return status;
  size = plan_out_size(error->size, sizeof(*error));
  memset(error, 0, size);
  if (size >= sizeof(size_t)) error->size = size;
  if (size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = status;
  if (size >= offsetof(DataBindError, line) + sizeof(error->line))
    error->line = -1;
  if (size >= offsetof(DataBindError, column) + sizeof(error->column))
    error->column = -1;
  if (size >= offsetof(DataBindError, message) + sizeof(error->message))
    snprintf(error->message, sizeof(error->message), "%s",
             message != NULL ? message : "DataBind projection plan failure");
  return status;
}

static char *plan_strdup(const char *text) {
  size_t length;
  char *copy;
  if (text == NULL) return NULL;
  length = strlen(text);
  if (length == SIZE_MAX) return NULL;
  copy = (char *)malloc(length + 1u);
  if (copy != NULL) memcpy(copy, text, length + 1u);
  return copy;
}

static int plan_format_valid(DataBindFormat format) {
  return format >= DATA_BIND_FORMAT_BINARY && format <= DATA_BIND_FORMAT_XML;
}

static int plan_format_uses_field_names(DataBindFormat format) {
  return format == DATA_BIND_FORMAT_JSON ||
         format == DATA_BIND_FORMAT_YAML ||
         format == DATA_BIND_FORMAT_CSV ||
         format == DATA_BIND_FORMAT_XML;
}

static uint32_t plan_format_states(DataBindFormat format) {
  switch (format) {
  case DATA_BIND_FORMAT_BINARY:
  case DATA_BIND_FORMAT_JSON:
  case DATA_BIND_FORMAT_YAML:
    return DATA_BIND_FORMAT_STATE_VALUE |
           DATA_BIND_FORMAT_STATE_ABSENT |
           DATA_BIND_FORMAT_STATE_NULL;
  case DATA_BIND_FORMAT_CSV:
  case DATA_BIND_FORMAT_XML:
    return DATA_BIND_FORMAT_STATE_VALUE |
           DATA_BIND_FORMAT_STATE_ABSENT;
  }
  return 0u;
}

static int plan_csv_root_kind_supported(DataBindSchemaKind kind) {
  return kind == DATA_BIND_SCHEMA_MESSAGE ||
         kind == DATA_BIND_SCHEMA_COMPOSITE;
}

static int plan_csv_nested_kind_supported(DataBindSchemaKind kind) {
  return kind == DATA_BIND_SCHEMA_ENUM ||
         kind == DATA_BIND_SCHEMA_FLAGS ||
         kind == DATA_BIND_SCHEMA_SCALAR;
}

static void plan_scan_reject_csv_field(
    DataBindPlanScan *scan,
    const DataBindSchemaField *field) {
  if (scan == NULL || field == NULL || scan->has_csv_unsupported_shape)
    return;
  scan->has_csv_unsupported_shape = 1;
  scan->csv_unsupported_field = field->name;
}

static int plan_xml_root_kind_supported(DataBindSchemaKind kind) {
  return kind == DATA_BIND_SCHEMA_MESSAGE ||
         kind == DATA_BIND_SCHEMA_COMPOSITE ||
         kind == DATA_BIND_SCHEMA_GROUP ||
         kind == DATA_BIND_SCHEMA_ENUM ||
         kind == DATA_BIND_SCHEMA_FLAGS ||
         kind == DATA_BIND_SCHEMA_SCALAR;
}

static int plan_xml_nested_kind_supported(DataBindSchemaKind kind) {
  return kind == DATA_BIND_SCHEMA_MESSAGE ||
         kind == DATA_BIND_SCHEMA_COMPOSITE ||
         kind == DATA_BIND_SCHEMA_ENUM ||
         kind == DATA_BIND_SCHEMA_FLAGS ||
         kind == DATA_BIND_SCHEMA_SCALAR;
}

static int plan_xml_sequence_supported(DataBind *codec, const DataBindSchemaField *field) {
  DataBindSchemaType element = DATA_BIND_SCHEMA_TYPE_INIT;
  if (!field->is_collection || field->is_map || field->is_group ||
      field->is_optional || field->is_nullable || field->inner_type == NULL ||
      strchr(field->inner_type, '<') != NULL || strcmp(field->inner_type, "bytes") == 0)
    return 0;
  return !data_bind_schema_find_type(codec, field->inner_type, &element) ||
         plan_xml_nested_kind_supported(element.kind);
}

static void plan_scan_reject_xml_field(
    DataBindPlanScan *scan,
    const DataBindSchemaField *field) {
  if (scan == NULL || field == NULL || scan->has_xml_unsupported_shape)
    return;
  scan->has_xml_unsupported_shape = 1;
  scan->xml_unsupported_field = field->name;
}

static int plan_slice_equal_cstr(
    const cserde_slice *slice, const char *text) {
  size_t length;
  if (slice == NULL || text == NULL ||
      !cserde_view_lifetime_valid(slice->lifetime) ||
      (slice->size != 0u && slice->data == NULL))
    return 0;
  length = strlen(text);
  return length == slice->size &&
         (length == 0u || memcmp(text, slice->data, length) == 0);
}

static int plan_type_requires_name_mapping(
    DataBind *codec, const char *type_name) {
  size_t field_count;
  size_t i;
  if (codec == NULL || type_name == NULL) return 0;
  field_count = data_bind_schema_field_count(codec, type_name);
  for (i = 0u; i < field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    size_t input_count;
    size_t j;
    if (!data_bind_schema_field_at(codec, type_name, i, &field) ||
        field.name == NULL)
      return 1;
    input_count =
        data_bind_internal_field_input_name_count(codec, type_name, i);
    for (j = 0u; j < input_count; ++j) {
      const char *accepted =
          data_bind_internal_field_input_name_at(codec, type_name, i, j);
      if (accepted != NULL && strcmp(accepted, field.name) != 0)
        return 1;
    }
  }
  return 0;
}

static DataBindStatus plan_name_map_append(
    DataBindFormatNames *names,
    const char *external_name,
    const char *canonical_name,
    DataBindError *error) {
  DataBindFormatNameMap empty = {NULL, NULL, UINT32_MAX, 0u};
  DataBindFormatNameMap *entry;
  stl_status status = DataBindFormatNames_push(names, empty);
  if (status != STL_OK)
    return plan_error(error,
        status == STL_CAPACITY_EXCEEDED ? DATA_BIND_ERR_LIMIT :
        status == STL_OUT_OF_MEMORY ? DATA_BIND_ERR_OOM : DATA_BIND_ERR_RUNTIME,
        "Unable to append FormatPlan field-name mapping");
  /* The unpublished plan owns partial entries too; Vec destroys them on failure. */
  entry = DataBindFormatNames_at(names, DataBindFormatNames_size(names) - 1u);
  if ((entry->external_name = tstr_dup(external_name)) == NULL ||
      (entry->canonical_name = tstr_dup(canonical_name)) == NULL)
    return plan_error(error, DATA_BIND_ERR_OOM,
                      "Unable to copy FormatPlan field-name mapping");
  return DATA_BIND_OK;
}

static DataBindStatus plan_add_name_map(
    DataBindFormatRecord *plan,
    const char *external_name,
    const char *canonical_name,
    DataBindError *error) {
  size_t i;

  if (plan == NULL || external_name == NULL ||
      external_name[0] == '\0' ||
      canonical_name == NULL || canonical_name[0] == '\0')
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "FormatPlan field-name mapping is incomplete");

  for (i = 0u; i < DataBindFormatNames_size(&plan->names); ++i) {
    const DataBindFormatNameMap *entry = DataBindFormatNames_at_const(&plan->names, i);
    if (strcmp(entry->external_name, external_name) != 0)
      continue;
    if (strcmp(entry->canonical_name, canonical_name) == 0)
      return DATA_BIND_OK;
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "FormatPlan input field name collides across canonical fields");
  }

  return plan_name_map_append(&plan->names, external_name, canonical_name, error);
}


static DataBindStatus plan_add_output_name(
    DataBindFormatRecord *plan,
    const char *external_name,
    const char *canonical_name,
    DataBindError *error) {
  size_t i;

  if (plan == NULL || external_name == NULL || external_name[0] == '\0' ||
      canonical_name == NULL || canonical_name[0] == '\0')
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "FormatPlan output field-name mapping is incomplete");

  for (i = 0u; i < DataBindFormatNames_size(&plan->outputs); ++i) {
    const DataBindFormatNameMap *entry = DataBindFormatNames_at_const(&plan->outputs, i);
    if (strcmp(entry->canonical_name, canonical_name) != 0)
      continue;
    if (strcmp(entry->external_name, external_name) == 0)
      return DATA_BIND_OK;
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "FormatPlan canonical field has multiple primary output names");
  }

  return plan_name_map_append(&plan->outputs, external_name, canonical_name, error);
}

static DataBindStatus plan_compile_record_name_map(
    DataBind *codec,
    const char *type_name,
    DataBindFormatRecord *plan,
    DataBindError *error) {
  size_t field_count;
  size_t name_limit = 0u;
  const size_t max_entries = SIZE_MAX / sizeof(DataBindFormatNameMap);
  size_t i;
  if (codec == NULL || type_name == NULL || plan == NULL)
    return plan_error(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Invalid FormatPlan name-map compile request");

  field_count = data_bind_schema_field_count(codec, type_name);
  if (field_count > max_entries)
    return plan_error(error, DATA_BIND_ERR_LIMIT,
                      "FormatPlan output names exceed addressable capacity");
  for (i = 0u; i < field_count; ++i) {
    size_t count = data_bind_internal_field_input_name_count(codec, type_name, i);
    if (count > max_entries - name_limit)
      return plan_error(error, DATA_BIND_ERR_LIMIT,
                        "FormatPlan input names exceed addressable capacity");
    name_limit += count;
  }
  /* Immutable schema cardinality bounds both tables, including duplicate aliases. */
  if (DataBindFormatNames_init(&plan->names, name_limit) != STL_OK ||
      DataBindFormatNames_init(&plan->outputs, field_count) != STL_OK)
    return plan_error(error, DATA_BIND_ERR_RUNTIME,
                      "Unable to initialize FormatPlan name storage");
  for (i = 0u; i < field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    size_t input_count;
    size_t j;
    if (!data_bind_schema_field_at(codec, type_name, i, &field) ||
        field.name == NULL)
      return plan_error(
          error, DATA_BIND_ERR_SCHEMA,
          "FormatPlan could not reflect one record field name");

    {
      const char *output_name =
          data_bind_internal_json_field_output_name(codec, type_name, i);
      DataBindStatus output_status;
      if (output_name == NULL || output_name[0] == '\0')
        return plan_error(
            error, DATA_BIND_ERR_SCHEMA,
            "FormatPlan record field has no primary output name");
      output_status = plan_add_output_name(
          plan, output_name, field.name, error);
      if (output_status != DATA_BIND_OK) return output_status;
    }

    input_count =
        data_bind_internal_field_input_name_count(codec, type_name, i);
    if (input_count == 0u)
      return plan_error(
          error, DATA_BIND_ERR_SCHEMA,
          "FormatPlan record field has no admitted input name");

    for (j = 0u; j < input_count; ++j) {
      const char *accepted =
          data_bind_internal_field_input_name_at(codec, type_name, i, j);
      DataBindStatus status;
      if (accepted == NULL || accepted[0] == '\0') continue;
      status = plan_add_name_map(
          plan, accepted, field.name, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

static const char *plan_canonical_name(
    const DataBindFormatRecord *plan,
    const cserde_slice *external_name) {
  size_t i;
  if (plan == NULL || external_name == NULL) return NULL;
  for (i = 0u; i < DataBindFormatNames_size(&plan->names); ++i) {
    const DataBindFormatNameMap *entry = DataBindFormatNames_at_const(&plan->names, i);
    if (plan_slice_equal_cstr(external_name, entry->external_name))
      return entry->canonical_name;
  }
  return NULL;
}

static const char *plan_external_name(
    const DataBindFormatRecord *plan,
    const cserde_slice *canonical_name) {
  size_t i;
  if (plan == NULL || canonical_name == NULL) return NULL;
  for (i = 0u; i < DataBindFormatNames_size(&plan->outputs); ++i) {
    const DataBindFormatNameMap *entry = DataBindFormatNames_at_const(&plan->outputs, i);
    if (plan_slice_equal_cstr(canonical_name, entry->canonical_name))
      return entry->external_name;
  }
  return NULL;
}

static int plan_reader_container_begin(cserde_token_kind kind) {
  return kind == CSERDE_MAP_BEGIN || kind == CSERDE_ARRAY_BEGIN;
}

static int plan_reader_container_end(cserde_token_kind kind) {
  return kind == CSERDE_MAP_END || kind == CSERDE_ARRAY_END;
}

static cserde_status plan_canonical_reader_next(
    void *context, cserde_token *out) {
  DataBindFormatCanonicalReader *state =
      (DataBindFormatCanonicalReader *)context;
  cserde_status status;
  cserde_token token;

  if (state == NULL || out == NULL ||
      state->source == NULL || state->plan == NULL)
    return CSERDE_INVALID_ARGUMENT;
  if (state->complete) return CSERDE_DONE;

  status = cserde_reader_next(state->source, &token);
  if (status != CSERDE_OK) return status;

  if (!state->root_started) {
    if (token.kind != CSERDE_MAP_BEGIN)
      return CSERDE_UNSUPPORTED;
    state->root_started = 1;
    state->expect_root_key = 1;
    *out = token;
    return CSERDE_OK;
  }

  if (state->value_depth != 0u) {
    if (plan_reader_container_begin(token.kind)) {
      if (state->value_depth == SIZE_MAX)
        return CSERDE_LIMIT_EXCEEDED;
      ++state->value_depth;
    } else if (plan_reader_container_end(token.kind)) {
      --state->value_depth;
      if (state->value_depth == 0u)
        state->expect_root_key = 1;
    }
    *out = token;
    return CSERDE_OK;
  }

  if (state->expect_root_key) {
    const char *canonical;
    if (token.kind == CSERDE_MAP_END) {
      state->complete = 1;
      *out = token;
      return CSERDE_OK;
    }
    if (token.kind != CSERDE_STRING)
      return CSERDE_UNSUPPORTED;
    canonical = plan_canonical_name(
        &state->plan->records[0], &token.value.slice);
    if (canonical == NULL) return CSERDE_UNSUPPORTED;
    token.value.slice.data =
        (const unsigned char *)canonical;
    token.value.slice.size = strlen(canonical);
    token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    state->expect_root_key = 0;
    *out = token;
    return CSERDE_OK;
  }

  if (plan_reader_container_end(token.kind))
    return CSERDE_UNSUPPORTED;
  if (plan_reader_container_begin(token.kind))
    state->value_depth = 1u;
  else
    state->expect_root_key = 1;
  *out = token;
  return CSERDE_OK;
}

static const cserde_reader_ops PLAN_CANONICAL_READER_OPS = {
    sizeof(cserde_reader_ops),
    CSERDE_READER_OPS_ABI_VERSION,
    plan_canonical_reader_next};

static int plan_scan_seen(
    const DataBindPlanScan *scan,
    const char *type_name) {
  size_t i;
  for (i = 0u; scan != NULL && i < scan->visited_count; ++i)
    if (scan->visited[i] != NULL &&
        strcmp(scan->visited[i], type_name) == 0)
      return 1;
  return 0;
}

static DataBindStatus plan_scan_type(
    DataBind *codec,
    const char *type_name,
    DataBindPlanScan *scan,
    DataBindError *error) {
  DataBindSchemaType type = DATA_BIND_SCHEMA_TYPE_INIT;
  size_t i;

  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      scan == NULL)
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG,
                      "Invalid FormatPlan schema scan");

  if (plan_scan_seen(scan, type_name)) {
    if (plan_type_requires_name_mapping(codec, type_name)) {
      scan->has_nested_name_mapping = 1;
    }
    return DATA_BIND_OK;
  }
  if (!data_bind_schema_find_type(codec, type_name, &type))
    return plan_error(error, DATA_BIND_ERR_TYPE_NOT_FOUND,
                      "FormatPlan type is not present in the DataBind schema");
  if (scan->visited_count >= DATA_BIND_PLAN_MAX_TYPES)
    return plan_error(error, DATA_BIND_ERR_LIMIT,
                      "FormatPlan schema graph exceeds the bounded type limit");

  if (scan->visited_count == 0u) {
    scan->root_kind = type.kind;
  } else if (plan_type_requires_name_mapping(codec, type_name) &&
             !scan->has_nested_name_mapping) {
    scan->has_nested_name_mapping = 1;
  }
  scan->visited[scan->visited_count++] =
      type.name != NULL ? type.name : type_name;

  for (i = 0u; i < data_bind_schema_field_count(codec, type_name); ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    const char *candidates[5];
    size_t j;
    if (!data_bind_schema_field_at(codec, type_name, i, &field))
      return plan_error(error, DATA_BIND_ERR_SCHEMA,
                        "FormatPlan could not reflect one schema field");

    if (field.is_optional) scan->has_optional = 1;
    if (field.is_nullable) scan->has_nullable = 1;

    if (field.is_collection || field.is_composite ||
        field.is_group || field.is_map) {
      plan_scan_reject_csv_field(scan, &field);
    } else if (field.type != NULL && field.type[0] != '\0') {
      DataBindSchemaType field_type = DATA_BIND_SCHEMA_TYPE_INIT;
      if (data_bind_schema_find_type(codec, field.type, &field_type) &&
          !plan_csv_nested_kind_supported(field_type.kind))
        plan_scan_reject_csv_field(scan, &field);
    }

    if (field.is_group || field.is_map ||
        (field.is_collection && !plan_xml_sequence_supported(codec, &field))) {
      plan_scan_reject_xml_field(scan, &field);
    } else if (field.type != NULL && field.type[0] != '\0') {
      DataBindSchemaType field_type = DATA_BIND_SCHEMA_TYPE_INIT;
      if (data_bind_schema_find_type(codec, field.type, &field_type) &&
          !plan_xml_nested_kind_supported(field_type.kind))
        plan_scan_reject_xml_field(scan, &field);
    }

    candidates[0] = field.type;
    candidates[1] = field.inner_type;
    candidates[2] = field.group_type;
    candidates[3] = field.value_type;
    candidates[4] = field.key_type;

    for (j = 0u; j < sizeof(candidates) / sizeof(candidates[0]); ++j) {
      DataBindSchemaType nested = DATA_BIND_SCHEMA_TYPE_INIT;
      const char *candidate = candidates[j];
      DataBindStatus status;
      if (candidate == NULL || candidate[0] == '\0' ||
          !data_bind_schema_find_type(codec, candidate, &nested))
        continue;
      status = plan_scan_type(codec, candidate, scan, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus plan_format_admit(
    DataBind *codec,
    const char *type_name,
    DataBindFormat format,
    int require_state_preservation,
    DataBindPlanScan *scan,
    DataBindError *error) {
  DataBindStatus status;
  uint32_t states = plan_format_states(format);
  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      !plan_format_valid(format))
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG,
                      "Invalid FormatPlan compile request");

  status = plan_scan_type(codec, type_name, scan, error);
  if (status != DATA_BIND_OK) return status;

  if (format == DATA_BIND_FORMAT_CSV) {
    char message[sizeof(((DataBindError *)0)->message)];
    if (!plan_csv_root_kind_supported(scan->root_kind))
      return plan_error(
          error, DATA_BIND_ERR_SCHEMA,
          "CSV FormatPlan requires a flat message/composite root");
    if (scan->has_csv_unsupported_shape) {
      if (scan->csv_unsupported_field != NULL)
        snprintf(message, sizeof(message),
                 "CSV FormatPlan field '%s' is not flat scalar/enum data; "
                 "explicit projection mapping is required",
                 scan->csv_unsupported_field);
      else
        snprintf(message, sizeof(message),
                 "CSV FormatPlan contains non-flat data; "
                 "explicit projection mapping is required");
      return plan_error(error, DATA_BIND_ERR_SCHEMA, message);
    }
  }

  if (format == DATA_BIND_FORMAT_XML) {
    char message[sizeof(((DataBindError *)0)->message)];
    if (!plan_xml_root_kind_supported(scan->root_kind))
      return plan_error(
          error, DATA_BIND_ERR_SCHEMA,
          "XML FormatPlan root cannot be represented without explicit projection mapping");
    if (scan->has_xml_unsupported_shape) {
      if (scan->xml_unsupported_field != NULL)
        snprintf(message, sizeof(message),
                 "XML FormatPlan field '%s' requires collection/variant projection mapping",
                 scan->xml_unsupported_field);
      else
        snprintf(message, sizeof(message),
                 "XML FormatPlan contains a shape that requires explicit projection mapping");
      return plan_error(error, DATA_BIND_ERR_SCHEMA, message);
    }
  }

  if (require_state_preservation && scan->has_nullable &&
      (states & DATA_BIND_FORMAT_STATE_NULL) == 0u)
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "Selected format cannot preserve explicit NULL for this contract");

  return DATA_BIND_OK;
}

/* The caller owns even a partially populated plan. Borrowed scan/schema data
 * never escapes this synchronous construction step. */
static DataBindStatus plan_format_populate(
    DataBindFormatPlan *plan,
    DataBind *codec,
    const char *type_name,
    const DataBindPlanScan *scan,
    DataBindError *error) {
  plan->type_name = plan_strdup(type_name);
  if (plan->type_name == NULL)
    return plan_error(error, DATA_BIND_ERR_OOM,
                      "Unable to copy FormatPlan type identity");
  plan->value_states = plan_format_states(plan->format);
  plan->root_kind = scan->root_kind;
  plan->has_optional = scan->has_optional;
  plan->has_nullable = scan->has_nullable;

  if (plan_format_uses_field_names(plan->format)) {
    size_t record;
    plan->has_nested_name_mapping = scan->has_nested_name_mapping;
    /* A direct record graph has unambiguous key positions. Collections and
     * variants require their own element/branch projection before names below
     * those boundaries can be admitted. Preserve fail-closed admission there. */
    if (scan->has_nested_name_mapping &&
        (scan->has_xml_unsupported_shape ||
         !plan_csv_root_kind_supported(scan->root_kind)))
      return plan_error(error, DATA_BIND_ERR_SCHEMA,
          "Contracts combining nested names with collections/variants require projection mapping");
    plan->record_count = scan->visited_count;
    for (record = 0u; record < plan->record_count; ++record) {
      DataBindSchemaType type = DATA_BIND_SCHEMA_TYPE_INIT;
      DataBindFormatRecord *node = &plan->records[record];
      DataBindStatus status;
      size_t field_index;
      if (!data_bind_schema_find_type(codec, scan->visited[record], &type))
        return plan_error(error, DATA_BIND_ERR_SCHEMA, "Missing projection record");
      if (type.kind != DATA_BIND_SCHEMA_MESSAGE &&
          type.kind != DATA_BIND_SCHEMA_COMPOSITE &&
          type.kind != DATA_BIND_SCHEMA_GROUP)
        continue;
      status = plan_compile_record_name_map(codec, scan->visited[record], node, error);
      if (status != DATA_BIND_OK) return status;
      for (field_index = 0u;
           field_index < data_bind_schema_field_count(codec, scan->visited[record]);
           ++field_index) {
        DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
        DataBindSchemaType child_type = DATA_BIND_SCHEMA_TYPE_INIT;
        size_t child, i;
        if (!data_bind_schema_field_at(codec, scan->visited[record], field_index, &field))
          return plan_error(error, DATA_BIND_ERR_SCHEMA, "Missing projection field");
        {
          int sequence = field.is_collection && !field.is_map && !field.is_group;
          if (sequence) plan->has_sequences = 1;
          const char *child_name = sequence ? field.inner_type : field.type;
          child = UINT32_MAX;
          if (!field.is_group && !field.is_map && child_name != NULL &&
              data_bind_schema_find_type(codec, child_name, &child_type) &&
              (child_type.kind == DATA_BIND_SCHEMA_MESSAGE ||
               child_type.kind == DATA_BIND_SCHEMA_COMPOSITE)) {
            for (child = 0u; child < plan->record_count; ++child)
              if (strcmp(scan->visited[child], child_type.name) == 0) break;
            if (child == plan->record_count)
              return plan_error(error, DATA_BIND_ERR_SCHEMA, "Unresolved projection child");
          }
          for (i = 0u; i < DataBindFormatNames_size(&node->names); ++i) {
            DataBindFormatNameMap *entry = DataBindFormatNames_at(&node->names, i);
            if (strcmp(entry->canonical_name, field.name) == 0) {
              entry->child = (uint32_t)child;
              entry->sequence = (uint32_t)sequence;
            }
          }
          for (i = 0u; i < DataBindFormatNames_size(&node->outputs); ++i) {
            DataBindFormatNameMap *entry = DataBindFormatNames_at(&node->outputs, i);
            if (strcmp(entry->canonical_name, field.name) == 0) {
              entry->child = (uint32_t)child;
              entry->sequence = (uint32_t)sequence;
            }
          }
        }
      }
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus plan_format_plan_compile(
    DataBind *codec,
    const char *type_name,
    DataBindFormat format,
    int require_state_preservation,
    DataBindFormatPlan **out_plan,
    DataBindError *error) {
  DataBindPlanScan scan = {0};
  DataBindFormatPlan *plan;
  DataBindStatus status;

  if (out_plan == NULL)
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG,
                      "FormatPlan output is required");
  *out_plan = NULL;
  status = plan_format_admit(
      codec, type_name, format, require_state_preservation, &scan, error);
  if (status != DATA_BIND_OK) return status;

  plan = (DataBindFormatPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return plan_error(error, DATA_BIND_ERR_OOM,
                      "Unable to allocate FormatPlan");
  plan->format = format;
  status = plan_format_populate(plan, codec, type_name, &scan, error);
  if (status != DATA_BIND_OK) {
    data_bind_format_plan_free(plan);
    return status;
  }
  /* Only complete plans cross the ownership boundary. */
  *out_plan = plan;
  return DATA_BIND_OK;
}


DataBindStatus data_bind_format_plan_compile(
    DataBind *codec,
    const char *type_name,
    DataBindFormat format,
    DataBindFormatPlan **out_plan,
    DataBindError *error) {
  return plan_format_plan_compile(
      codec, type_name, format, 1, out_plan, error);
}

DataBindStatus data_bind_format_plan_compile_reader(
    DataBind *codec,
    const char *type_name,
    DataBindFormat format,
    DataBindFormatPlan **out_plan,
    DataBindError *error) {
  return plan_format_plan_compile(
      codec, type_name, format, 0, out_plan, error);
}

void data_bind_format_plan_free(DataBindFormatPlan *plan) {
  size_t i;
  if (plan == NULL) return;
  for (i = 0u; i < plan->record_count; ++i) {
    DataBindFormatNames_destroy(&plan->records[i].names);
    DataBindFormatNames_destroy(&plan->records[i].outputs);
  }
  free(plan->type_name);
  free(plan);
}

int data_bind_format_plan_info(
    const DataBindFormatPlan *plan,
    DataBindFormatPlanInfo *out) {
  size_t size;
  DataBindFormatPlanInfo full;
  if (plan == NULL || out == NULL || out->size < sizeof(size_t))
    return 0;
  size = plan_out_size(out->size, sizeof(*out));
  full = (DataBindFormatPlanInfo){
      sizeof(DataBindFormatPlanInfo),
      DATA_BIND_PROJECTION_PLAN_ABI_VERSION,
      plan->type_name,
      plan->format,
      plan->value_states,
      plan->has_optional,
      plan->has_nullable};
  memset(out, 0, size);
  memcpy(out, &full, size);
  out->size = size;
  return 1;
}

static const cserde_reader_ops PLAN_RECURSIVE_READER_OPS;
static const cserde_writer_ops PLAN_RECURSIVE_WRITER_OPS;

static DataBindStatus plan_canonical_reader_init(
    const DataBindFormatPlan *plan,
    cserde_reader *source,
    DataBindFormatCanonicalReader *out,
    DataBindFormatCursor *cursor,
    DataBindError *error) {
  size_t size;
  DataBindFormatCanonicalReader initial =
      DATA_BIND_FORMAT_CANONICAL_READER_INIT;
  cserde_status reader_status;

  if (plan == NULL || source == NULL || out == NULL ||
      out->size < sizeof(*out))
    return plan_error(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Invalid FormatPlan canonical reader arguments");
  if (!plan_format_uses_field_names(plan->format))
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "Selected FormatPlan does not use parser field-name canonicalization");
  if (plan->root_kind != DATA_BIND_SCHEMA_MESSAGE &&
      plan->root_kind != DATA_BIND_SCHEMA_COMPOSITE &&
      plan->root_kind != DATA_BIND_SCHEMA_GROUP)
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "FormatPlan canonical reader requires a record root");

  if (cursor == NULL && plan->has_nested_name_mapping)
    return plan_error(error, DATA_BIND_ERR_SCHEMA,
                      "Nested names require a recursive FormatPlan cursor");
  size = sizeof(*out);
  memset(out, 0, size);
  initial.size = size;
  initial.plan = plan;
  initial.source = source;
  memcpy(out, &initial, size);

  if (cursor != NULL) {
    *cursor = (DataBindFormatCursor)DATA_BIND_FORMAT_CURSOR_INIT;
    cursor->owner = out;
  }
  reader_status = cserde_reader_init(
      &out->reader, cursor != NULL ? &PLAN_RECURSIVE_READER_OPS : &PLAN_CANONICAL_READER_OPS,
      cursor != NULL ? (void *)cursor : (void *)out);
  if (reader_status != CSERDE_OK) {
    memset(out, 0, size);
    out->size = size;
    return plan_error(
        error, DATA_BIND_ERR_RUNTIME,
        "Could not initialize FormatPlan canonical CSerde reader");
  }

  if (error != NULL)
    (void)plan_error(error, DATA_BIND_OK, "");
  return DATA_BIND_OK;
}

cserde_reader *data_bind_format_canonical_reader_reader(
    DataBindFormatCanonicalReader *reader) {
  if (reader == NULL ||
      reader->size < sizeof(*reader) ||
      reader->abi_version !=
          DATA_BIND_FORMAT_CANONICAL_READER_ABI_VERSION ||
      reader->reader.state == CSERDE_READER_ZERO)
    return NULL;
  return &reader->reader;
}

static cserde_status plan_canonical_writer_status(cserde_status status) {
  switch (status) {
  case CSERDE_OK:
  case CSERDE_LIMIT_EXCEEDED:
  case CSERDE_UNSUPPORTED:
  case CSERDE_SINK_ERROR:
    return status;
  default:
    return CSERDE_UNSUPPORTED;
  }
}

static cserde_status plan_canonical_writer_write(
    void *context, const cserde_token *input) {
  DataBindFormatCanonicalWriter *state =
      (DataBindFormatCanonicalWriter *)context;
  cserde_token token;
  cserde_status status;

  if (state == NULL || input == NULL || state->target == NULL ||
      state->plan == NULL || state->complete)
    return CSERDE_UNSUPPORTED;
  token = *input;

  if (!state->root_started) {
    if (token.kind != CSERDE_MAP_BEGIN) return CSERDE_UNSUPPORTED;
    state->root_started = 1;
    state->expect_root_key = 1;
    return plan_canonical_writer_status(
        cserde_writer_write(state->target, &token));
  }

  if (state->value_depth != 0u) {
    if (plan_reader_container_begin(token.kind)) {
      if (state->value_depth == SIZE_MAX) return CSERDE_LIMIT_EXCEEDED;
      ++state->value_depth;
    } else if (plan_reader_container_end(token.kind)) {
      --state->value_depth;
      if (state->value_depth == 0u) state->expect_root_key = 1;
    }
    return plan_canonical_writer_status(
        cserde_writer_write(state->target, &token));
  }

  if (state->expect_root_key) {
    const char *external;
    if (token.kind == CSERDE_MAP_END) {
      state->complete = 1;
      return plan_canonical_writer_status(
          cserde_writer_write(state->target, &token));
    }
    if (token.kind != CSERDE_STRING) return CSERDE_UNSUPPORTED;
    external = plan_external_name(&state->plan->records[0], &token.value.slice);
    if (external == NULL) return CSERDE_UNSUPPORTED;
    token.value.slice.data = (const unsigned char *)external;
    token.value.slice.size = strlen(external);
    token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    state->expect_root_key = 0;
    return plan_canonical_writer_status(
        cserde_writer_write(state->target, &token));
  }

  if (plan_reader_container_end(token.kind)) return CSERDE_UNSUPPORTED;
  if (plan_reader_container_begin(token.kind))
    state->value_depth = 1u;
  else
    state->expect_root_key = 1;
  if (state->plan->format == DATA_BIND_FORMAT_JSON && token.kind == CSERDE_BYTES) {
    /* DataBind owns this text representation; the parser stays token-strict. */
    if (!vstr_utf8_valid(vstr_from_buf(
            (const char *)token.value.slice.data, token.value.slice.size)))
      return CSERDE_UNSUPPORTED;
    token.kind = CSERDE_STRING;
  }
  status = cserde_writer_write(state->target, &token);
  return plan_canonical_writer_status(status);
}

static cserde_status plan_canonical_writer_finish(void *context) {
  const DataBindFormatCanonicalWriter *state =
      (const DataBindFormatCanonicalWriter *)context;
  if (state == NULL || !state->root_started || !state->complete ||
      state->value_depth != 0u || !state->expect_root_key)
    return CSERDE_UNSUPPORTED;
  return CSERDE_OK;
}

static const cserde_writer_ops PLAN_CANONICAL_WRITER_OPS = {
    sizeof(cserde_writer_ops),
    CSERDE_WRITER_OPS_ABI_VERSION,
    plan_canonical_writer_write,
    plan_canonical_writer_finish};

static DataBindStatus plan_canonical_writer_init(
    const DataBindFormatPlan *plan,
    cserde_writer *target,
    DataBindFormatCanonicalWriter *out,
    DataBindFormatCursor *cursor,
    DataBindError *error) {
  DataBindFormatCanonicalWriter initial =
      DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
  cserde_status writer_status;
  size_t size;

  if (plan == NULL || target == NULL || out == NULL ||
      out->size < sizeof(*out) ||
      target->state != CSERDE_WRITER_READY)
    return plan_error(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Invalid FormatPlan canonical writer arguments");
  if (!plan_format_uses_field_names(plan->format))
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "Selected FormatPlan does not use parser field-name projection");
  if (plan->root_kind != DATA_BIND_SCHEMA_MESSAGE &&
      plan->root_kind != DATA_BIND_SCHEMA_COMPOSITE &&
      plan->root_kind != DATA_BIND_SCHEMA_GROUP)
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "FormatPlan canonical writer requires a record root");

  if (cursor == NULL && plan->has_nested_name_mapping)
    return plan_error(error, DATA_BIND_ERR_SCHEMA,
                      "Nested names require a recursive FormatPlan cursor");
  size = sizeof(*out);
  memset(out, 0, size);
  initial.size = size;
  initial.plan = plan;
  initial.target = target;
  memcpy(out, &initial, size);

  if (cursor != NULL) {
    *cursor = (DataBindFormatCursor)DATA_BIND_FORMAT_CURSOR_INIT;
    cursor->owner = out;
  }
  writer_status = cserde_writer_init(
      &out->writer, cursor != NULL ? &PLAN_RECURSIVE_WRITER_OPS : &PLAN_CANONICAL_WRITER_OPS,
      cursor != NULL ? (void *)cursor : (void *)out);
  if (writer_status != CSERDE_OK) {
    memset(out, 0, size);
    out->size = size;
    return plan_error(
        error, DATA_BIND_ERR_RUNTIME,
        "Could not initialize FormatPlan canonical CSerde writer");
  }

  if (error != NULL) (void)plan_error(error, DATA_BIND_OK, "");
  return DATA_BIND_OK;
}

cserde_writer *data_bind_format_canonical_writer_writer(
    DataBindFormatCanonicalWriter *writer) {
  if (writer == NULL || writer->size < sizeof(*writer) ||
      writer->abi_version != DATA_BIND_FORMAT_CANONICAL_WRITER_ABI_VERSION ||
      writer->writer.state == CSERDE_WRITER_ZERO)
    return NULL;
  return &writer->writer;
}

/* One cursor owns traversal state; immutable plan tables can be shared by any
 * number of concurrent readers/writers. Only keys are retained (in the plan),
 * never source token slices. Unknown container shapes pass through unchanged. */
static cserde_status plan_cursor_token(
    const DataBindFormatPlan *plan, DataBindFormatCursor *cursor,
    cserde_token *token, int writing, int *started, int *complete) {
  DataBindFormatCursorFrame *frame;
  uint32_t child = UINT32_MAX;
  if (!*started) {
    if (token->kind != CSERDE_MAP_BEGIN) return CSERDE_UNSUPPORTED;
    cursor->frames[0] = (DataBindFormatCursorFrame){0u, UINT32_MAX, CSERDE_MAP_BEGIN, 1};
    cursor->depth = 1u;
    *started = 1;
    return CSERDE_OK;
  }
  if (*complete || cursor->depth == 0u) return CSERDE_UNSUPPORTED;
  frame = &cursor->frames[cursor->depth - 1u];
  if (plan_reader_container_end(token->kind)) {
    if ((frame->kind == CSERDE_MAP_BEGIN &&
         (token->kind != CSERDE_MAP_END || !frame->expect_key)) ||
        (frame->kind == CSERDE_ARRAY_BEGIN && token->kind != CSERDE_ARRAY_END))
      return CSERDE_UNSUPPORTED;
    if (--cursor->depth == 0u) *complete = 1;
    return CSERDE_OK;
  }
  if (frame->kind == CSERDE_MAP_BEGIN && frame->expect_key) {
    if (plan_reader_container_begin(token->kind)) return CSERDE_UNSUPPORTED;
    frame->child = UINT32_MAX;
    if (frame->record != UINT32_MAX) {
      const DataBindFormatRecord *record = &plan->records[frame->record];
      const DataBindFormatNames *names = writing ? &record->outputs : &record->names;
      const DataBindFormatNameMap *entry = NULL;
      size_t i;
      if (token->kind != CSERDE_STRING) return CSERDE_UNSUPPORTED;
      for (i = 0u; i < DataBindFormatNames_size(names); ++i) {
        const DataBindFormatNameMap *candidate = DataBindFormatNames_at_const(names, i);
        if (plan_slice_equal_cstr(&token->value.slice,
                writing ? candidate->canonical_name : candidate->external_name)) {
          entry = candidate;
          break;
        }
      }
      if (entry == NULL) return CSERDE_UNSUPPORTED;
      token->value.slice.data = (const unsigned char *)(
          writing ? entry->external_name : entry->canonical_name);
      token->value.slice.size = strlen((const char *)token->value.slice.data);
      token->value.slice.lifetime = CSERDE_VIEW_STABLE;
      frame->child = entry->child;
    }
    frame->expect_key = 0;
    return CSERDE_OK;
  }
  if (plan_reader_container_begin(token->kind) &&
      cursor->depth == DATA_BIND_FORMAT_CURSOR_MAX_DEPTH)
    return CSERDE_LIMIT_EXCEEDED;
  if (frame->kind == CSERDE_ARRAY_BEGIN) child = frame->record;
  if (frame->kind == CSERDE_MAP_BEGIN) {
    child = frame->child;
    frame->expect_key = 1;
    frame->child = UINT32_MAX;
  }
  if (plan_reader_container_begin(token->kind)) {
    cursor->frames[cursor->depth++] = (DataBindFormatCursorFrame){
        child, UINT32_MAX, token->kind, 1};
  } else if (writing && cursor->depth == 1u &&
             plan->format == DATA_BIND_FORMAT_JSON && token->kind == CSERDE_BYTES) {
    if (!vstr_utf8_valid(vstr_from_buf(
            (const char *)token->value.slice.data, token->value.slice.size)))
      return CSERDE_UNSUPPORTED;
    token->kind = CSERDE_STRING;
  }
  return CSERDE_OK;
}

static cserde_status plan_recursive_reader_next(void *context, cserde_token *out) {
  DataBindFormatCursor *cursor = (DataBindFormatCursor *)context;
  DataBindFormatCanonicalReader *state = (DataBindFormatCanonicalReader *)cursor->owner;
  cserde_status status;
  if (state->complete) return CSERDE_DONE;
  status = cserde_reader_next(state->source, out);
  if (status != CSERDE_OK) return status;
  return plan_cursor_token(state->plan, cursor, out, 0,
                           &state->root_started, &state->complete);
}

static const cserde_reader_ops PLAN_RECURSIVE_READER_OPS = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION,
    plan_recursive_reader_next};

static cserde_status plan_recursive_writer_write(void *context, const cserde_token *input) {
  DataBindFormatCursor *cursor = (DataBindFormatCursor *)context;
  DataBindFormatCanonicalWriter *state = (DataBindFormatCanonicalWriter *)cursor->owner;
  cserde_token token = *input;
  cserde_status status = plan_cursor_token(state->plan, cursor, &token, 1,
                                          &state->root_started, &state->complete);
  if (status != CSERDE_OK) return status;
  return plan_canonical_writer_status(cserde_writer_write(state->target, &token));
}

static cserde_status plan_recursive_writer_finish(void *context) {
  const DataBindFormatCursor *cursor = (const DataBindFormatCursor *)context;
  const DataBindFormatCanonicalWriter *state =
      (const DataBindFormatCanonicalWriter *)cursor->owner;
  return state->complete && cursor->depth == 0u ? CSERDE_OK : CSERDE_UNSUPPORTED;
}

static const cserde_writer_ops PLAN_RECURSIVE_WRITER_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    plan_recursive_writer_write, plan_recursive_writer_finish};

DataBindStatus data_bind_format_canonical_reader_init(
    const DataBindFormatPlan *plan, cserde_reader *source,
    DataBindFormatCanonicalReader *out, DataBindError *error) {
  return plan_canonical_reader_init(plan, source, out, NULL, error);
}

DataBindStatus data_bind_format_canonical_writer_init(
    const DataBindFormatPlan *plan, cserde_writer *target,
    DataBindFormatCanonicalWriter *out, DataBindError *error) {
  return plan_canonical_writer_init(plan, target, out, NULL, error);
}

DataBindStatus data_bind_format_canonical_reader_init_recursive(
    const DataBindFormatPlan *plan, cserde_reader *source,
    DataBindFormatCanonicalReader *out, DataBindFormatCursor *cursor,
    DataBindError *error) {
  if (cursor == NULL || cursor->size < sizeof(*cursor))
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG, "Invalid FormatPlan cursor");
  return plan_canonical_reader_init(plan, source, out, cursor, error);
}

DataBindStatus data_bind_format_canonical_writer_init_recursive(
    const DataBindFormatPlan *plan, cserde_writer *target,
    DataBindFormatCanonicalWriter *out, DataBindFormatCursor *cursor,
    DataBindError *error) {
  if (cursor == NULL || cursor->size < sizeof(*cursor))
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG, "Invalid FormatPlan cursor");
  return plan_canonical_writer_init(plan, target, out, cursor, error);
}

int data_bind_format_plan_xml_record(const DataBindFormatPlan *plan) {
  return plan != NULL && plan->format == DATA_BIND_FORMAT_XML &&
         plan_csv_root_kind_supported(plan->root_kind);
}

int data_bind_format_plan_has_sequences(const DataBindFormatPlan *plan) {
  return plan != NULL && plan->has_sequences;
}

size_t data_bind_format_record_field_count(const DataBindFormatPlan *plan, uint32_t record) {
  return plan != NULL && record < plan->record_count
      ? DataBindFormatNames_size(&plan->records[record].outputs) : 0u;
}

int data_bind_format_record_field_at(const DataBindFormatPlan *plan, uint32_t record,
                                    size_t index, DataBindXmlFieldPlan *out) {
  const DataBindFormatNameMap *entry;
  if (out == NULL || index >= data_bind_format_record_field_count(plan, record)) return 0;
  entry = DataBindFormatNames_at_const(&plan->records[record].outputs, index);
  *out = (DataBindXmlFieldPlan){entry->canonical_name, entry->child, (int)entry->sequence};
  return 1;
}

size_t data_bind_format_record_field_find(const DataBindFormatPlan *plan, uint32_t record,
                                        const char *name, size_t length) {
  cserde_slice key = {(const unsigned char *)name, length, CSERDE_VIEW_STABLE};
  const char *canonical;
  size_t i;
  if (plan == NULL || record >= plan->record_count) return SIZE_MAX;
  canonical = plan_canonical_name(&plan->records[record], &key);
  if (canonical == NULL) return SIZE_MAX;
  for (i = 0u; i < DataBindFormatNames_size(&plan->records[record].outputs); ++i)
    if (strcmp(DataBindFormatNames_at_const(&plan->records[record].outputs, i)->canonical_name,
               canonical) == 0) return i;
  return SIZE_MAX;
}

static int plan_transport_kind_valid(DataBindTransportKind kind) {
  return kind == DATA_BIND_TRANSPORT_HTTP ||
         kind == DATA_BIND_TRANSPORT_RPC;
}

static DataBindStatus plan_transport_populate(
    DataBindTransportPlan *plan,
    DataBind *codec,
    const DataBindServiceOperation *operation,
    DataBindFormat ingress_format,
    DataBindFormat egress_format,
    DataBindError *error) {
  DataBindStatus status;
  if (operation->request_type != NULL &&
      strcmp(operation->request_type, "void") != 0) {
    status = data_bind_format_plan_compile(
        codec, operation->request_type, ingress_format, &plan->ingress, error);
    if (status != DATA_BIND_OK) return status;
  }
  if (operation->response_type != NULL &&
      strcmp(operation->response_type, "void") != 0)
    return data_bind_format_plan_compile(
        codec, operation->response_type, egress_format, &plan->egress, error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_transport_plan_compile_service(
    DataBind *codec,
    const char *service_name,
    const char *operation_name,
    DataBindTransportKind kind,
    DataBindFormat ingress_format,
    DataBindFormat egress_format,
    DataBindTransportPlan **out_plan,
    DataBindError *error) {
  DataBindServiceOperation operation = DATA_BIND_SERVICE_OPERATION_INIT;
  DataBindTransportPlan *plan = NULL;
  DataBindStatus status;

  if (out_plan == NULL)
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG,
                      "TransportPlan output is required");
  *out_plan = NULL;
  if (codec == NULL || service_name == NULL || service_name[0] == '\0' ||
      operation_name == NULL || operation_name[0] == '\0' ||
      !plan_transport_kind_valid(kind) ||
      !plan_format_valid(ingress_format) ||
      !plan_format_valid(egress_format))
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG,
                      "Invalid TransportPlan compile request");

  if (!data_bind_service_operation_find(
          codec, service_name, operation_name, &operation))
    return plan_error(error, DATA_BIND_ERR_SCHEMA,
                      "TransportPlan service operation was not found");

  plan = (DataBindTransportPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return plan_error(error, DATA_BIND_ERR_OOM,
                      "Unable to allocate TransportPlan");
  plan->kind = kind;
  plan->service_name = plan_strdup(service_name);
  plan->operation_name = plan_strdup(operation_name);
  if (plan->service_name == NULL || plan->operation_name == NULL) {
    status = plan_error(error, DATA_BIND_ERR_OOM,
                      "Unable to copy TransportPlan identity");
  } else {
    status = plan_transport_populate(
        plan, codec, &operation, ingress_format, egress_format, error);
  }
  if (status != DATA_BIND_OK) {
    data_bind_transport_plan_free(plan);
    return status;
  }

  *out_plan = plan;
  return DATA_BIND_OK;
}

void data_bind_transport_plan_free(DataBindTransportPlan *plan) {
  if (plan == NULL) return;
  data_bind_format_plan_free(plan->ingress);
  data_bind_format_plan_free(plan->egress);
  free(plan->service_name);
  free(plan->operation_name);
  free(plan);
}

int data_bind_transport_plan_info(
    const DataBindTransportPlan *plan,
    DataBindTransportPlanInfo *out) {
  size_t size;
  DataBindTransportPlanInfo full;
  if (plan == NULL || out == NULL || out->size < sizeof(size_t))
    return 0;
  size = plan_out_size(out->size, sizeof(*out));
  full = (DataBindTransportPlanInfo){
      sizeof(DataBindTransportPlanInfo),
      DATA_BIND_PROJECTION_PLAN_ABI_VERSION,
      plan->kind,
      plan->service_name,
      plan->operation_name,
      plan->ingress,
      plan->egress};
  memset(out, 0, size);
  memcpy(out, &full, size);
  out->size = size;
  return 1;
}

#include "data_bind_projection_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DATA_BIND_PLAN_MAX_TYPES = 64u };

struct DataBindFormatPlan {
  char *type_name;
  DataBindFormat format;
  uint32_t value_states;
  int has_optional;
  int has_nullable;
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

static void plan_scan_reject_xml_field(
    DataBindPlanScan *scan,
    const DataBindSchemaField *field) {
  if (scan == NULL || field == NULL || scan->has_xml_unsupported_shape)
    return;
  scan->has_xml_unsupported_shape = 1;
  scan->xml_unsupported_field = field->name;
}

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

  if (plan_scan_seen(scan, type_name)) return DATA_BIND_OK;
  if (!data_bind_schema_find_type(codec, type_name, &type))
    return plan_error(error, DATA_BIND_ERR_TYPE_NOT_FOUND,
                      "FormatPlan type is not present in the DataBind schema");
  if (scan->visited_count >= DATA_BIND_PLAN_MAX_TYPES)
    return plan_error(error, DATA_BIND_ERR_LIMIT,
                      "FormatPlan schema graph exceeds the bounded type limit");

  if (scan->visited_count == 0u) scan->root_kind = type.kind;
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

    if (field.is_collection || field.is_group || field.is_map) {
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

DataBindStatus data_bind_format_plan_compile(
    DataBind *codec,
    const char *type_name,
    DataBindFormat format,
    DataBindFormatPlan **out_plan,
    DataBindError *error) {
  DataBindPlanScan scan = {0};
  DataBindFormatPlan *plan;
  DataBindStatus status;
  uint32_t states;

  if (out_plan == NULL)
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG,
                      "FormatPlan output is required");
  *out_plan = NULL;
  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      !plan_format_valid(format))
    return plan_error(error, DATA_BIND_ERR_INVALID_ARG,
                      "Invalid FormatPlan compile request");

  states = plan_format_states(format);
  status = plan_scan_type(codec, type_name, &scan, error);
  if (status != DATA_BIND_OK) return status;

  if (format == DATA_BIND_FORMAT_CSV) {
    char message[sizeof(((DataBindError *)0)->message)];
    if (!plan_csv_root_kind_supported(scan.root_kind))
      return plan_error(
          error, DATA_BIND_ERR_SCHEMA,
          "CSV FormatPlan requires a flat message/composite root");
    if (scan.has_csv_unsupported_shape) {
      if (scan.csv_unsupported_field != NULL)
        snprintf(message, sizeof(message),
                 "CSV FormatPlan field '%s' is not flat scalar/enum data; "
                 "explicit projection mapping is required",
                 scan.csv_unsupported_field);
      else
        snprintf(message, sizeof(message),
                 "CSV FormatPlan contains non-flat data; "
                 "explicit projection mapping is required");
      return plan_error(error, DATA_BIND_ERR_SCHEMA, message);
    }
  }

  if (format == DATA_BIND_FORMAT_XML) {
    char message[sizeof(((DataBindError *)0)->message)];
    if (!plan_xml_root_kind_supported(scan.root_kind))
      return plan_error(
          error, DATA_BIND_ERR_SCHEMA,
          "XML FormatPlan root cannot be represented without explicit projection mapping");
    if (scan.has_xml_unsupported_shape) {
      if (scan.xml_unsupported_field != NULL)
        snprintf(message, sizeof(message),
                 "XML FormatPlan field '%s' requires collection/variant projection mapping",
                 scan.xml_unsupported_field);
      else
        snprintf(message, sizeof(message),
                 "XML FormatPlan contains a shape that requires explicit projection mapping");
      return plan_error(error, DATA_BIND_ERR_SCHEMA, message);
    }
  }

  if (scan.has_nullable &&
      (states & DATA_BIND_FORMAT_STATE_NULL) == 0u)
    return plan_error(
        error, DATA_BIND_ERR_SCHEMA,
        "Selected format cannot preserve explicit NULL for this contract");

  plan = (DataBindFormatPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return plan_error(error, DATA_BIND_ERR_OOM,
                      "Unable to allocate FormatPlan");
  plan->type_name = plan_strdup(type_name);
  if (plan->type_name == NULL) {
    free(plan);
    return plan_error(error, DATA_BIND_ERR_OOM,
                      "Unable to copy FormatPlan type identity");
  }
  plan->format = format;
  plan->value_states = states;
  plan->has_optional = scan.has_optional;
  plan->has_nullable = scan.has_nullable;
  *out_plan = plan;
  return DATA_BIND_OK;
}

void data_bind_format_plan_free(DataBindFormatPlan *plan) {
  if (plan == NULL) return;
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

static int plan_transport_kind_valid(DataBindTransportKind kind) {
  return kind == DATA_BIND_TRANSPORT_HTTP ||
         kind == DATA_BIND_TRANSPORT_RPC;
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
    data_bind_transport_plan_free(plan);
    return plan_error(error, DATA_BIND_ERR_OOM,
                      "Unable to copy TransportPlan identity");
  }

  if (operation.request_type != NULL &&
      strcmp(operation.request_type, "void") != 0) {
    status = data_bind_format_plan_compile(
        codec, operation.request_type, ingress_format,
        &plan->ingress, error);
    if (status != DATA_BIND_OK) {
      data_bind_transport_plan_free(plan);
      return status;
    }
  }

  if (operation.response_type != NULL &&
      strcmp(operation.response_type, "void") != 0) {
    status = data_bind_format_plan_compile(
        codec, operation.response_type, egress_format,
        &plan->egress, error);
    if (status != DATA_BIND_OK) {
      data_bind_transport_plan_free(plan);
      return status;
    }
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

#include "data_bind_xml_provider.h"

#include <xml_parser/xml_parser.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum data_bind_xml_phase {
  DATA_BIND_XML_CHILD_KEY = 0,
  DATA_BIND_XML_CHILD_VALUE,
  DATA_BIND_XML_ATTRIBUTE_KEY,
  DATA_BIND_XML_ATTRIBUTE_VALUE,
  DATA_BIND_XML_END
} data_bind_xml_phase;

typedef struct data_bind_xml_frame {
  salts_xml_node node;
  size_t child_index;
  size_t attribute_index;
  salts_xml_node pending_child;
  salts_xml_attribute pending_attribute;
  data_bind_xml_phase phase;
} data_bind_xml_frame;

typedef struct data_bind_xml_reader {
  cserde_reader reader;
  salts_xml_document document;
  salts_xml_node root;
  salts_xml_node_list selected;
  size_t max_depth;
  size_t depth;
  size_t selected_index;
  int root_pending;
  int selected_array;
  int array_started;
  int array_finished;
  data_bind_xml_frame frames[];
} data_bind_xml_reader;

static DataBindStatus xml_provider_error(
    DataBindError *error,
    DataBindStatus status,
    const char *message) {
  if (error != NULL && error->size >= sizeof(error->size)) {
    if (error->size >= offsetof(DataBindError, code) + sizeof(error->code))
      error->code = status;
    if (error->size >= offsetof(DataBindError, line) + sizeof(error->line))
      error->line = -1;
    if (error->size >= offsetof(DataBindError, column) + sizeof(error->column))
      error->column = -1;
    if (error->size >= offsetof(DataBindError, path) + sizeof(error->path))
      error->path[0] = '\0';
    if (error->size >= offsetof(DataBindError, message) + sizeof(error->message))
      snprintf(error->message, sizeof(error->message), "%s",
               message != NULL ? message : "");
  }
  return status;
}

static DataBindQueryStatus xml_query_status(qvm_status_t status) {
  switch (status) {
  case QVM_STATUS_OK:
    return DATA_BIND_QUERY_OK;
  case QVM_STATUS_INVALID_ARGUMENT:
    return DATA_BIND_QUERY_INVALID_ARGUMENT;
  case QVM_STATUS_INVALID_PROGRAM:
    return DATA_BIND_QUERY_INVALID_PROGRAM;
  case QVM_STATUS_UNSUPPORTED:
    return DATA_BIND_QUERY_UNSUPPORTED;
  case QVM_STATUS_BACKEND_ERROR:
    return DATA_BIND_QUERY_BACKEND_ERROR;
  case QVM_STATUS_NO_MEMORY:
    return DATA_BIND_QUERY_NO_MEMORY;
  case QVM_STATUS_RESOURCE_LIMIT:
    return DATA_BIND_QUERY_RESOURCE_LIMIT;
  case QVM_STATUS_BUFFER_TOO_SMALL:
    return DATA_BIND_QUERY_BUFFER_TOO_SMALL;
  default:
    return DATA_BIND_QUERY_BACKEND_ERROR;
  }
}

static qvm_limits_t xml_query_limits(const DataBindQueryLimits *limits) {
  if (limits == NULL) return qvm_default_limits();
  return (qvm_limits_t){
      limits->max_instructions, limits->max_operands,
      limits->max_regexes, limits->max_steps};
}

static void xml_query_diagnostic(
    DataBindQueryDiagnostic *out,
    const qvm_diagnostic_t *native) {
  size_t size;
  if (out == NULL || out->size < sizeof(*out) || native == NULL) return;
  size = out->size;
  *out = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  out->size = size;
  out->status = xml_query_status(native->status);
  out->instruction = native->instruction;
  out->opcode = native->opcode;
  out->operand = native->operand;
  snprintf(out->message, sizeof(out->message), "%s",
           native->message != NULL ? native->message : "");
}

static DataBindStatus xml_query_failure(
    const DataBindQueryDiagnostic *diagnostic) {
  if (diagnostic == NULL) return DATA_BIND_ERR_PARSE;
  switch (diagnostic->status) {
  case DATA_BIND_QUERY_RESOURCE_LIMIT:
    return DATA_BIND_ERR_LIMIT;
  case DATA_BIND_QUERY_NO_MEMORY:
    return DATA_BIND_ERR_OOM;
  case DATA_BIND_QUERY_INVALID_ARGUMENT:
    return DATA_BIND_ERR_INVALID_ARG;
  default:
    return DATA_BIND_ERR_PARSE;
  }
}

static void xml_emit_slice(cserde_token *out, salts_xml_string_view view) {
  memset(out, 0, sizeof(*out));
  out->kind = CSERDE_STRING;
  out->value.slice.data = (const unsigned char *)view.data;
  out->value.slice.size = view.size;
  out->value.slice.lifetime = CSERDE_VIEW_STABLE;
}

static int xml_view_equal(salts_xml_string_view left,
                          salts_xml_string_view right) {
  return left.size == right.size &&
      (left.size == 0u ||
       (left.data != NULL && right.data != NULL &&
        memcmp(left.data, right.data, left.size) == 0));
}

static int xml_node_has_element_child(salts_xml_node node) {
  size_t count = salts_xml_node_child_count(node);
  size_t index;
  for (index = 0u; index < count; ++index) {
    if (salts_xml_node_type(salts_xml_node_child_at(node, index)) ==
        SALTS_XML_ELEMENT)
      return 1;
  }
  return 0;
}

static int xml_attribute_shadowed_by_child(
    salts_xml_node node,
    salts_xml_attribute attribute) {
  salts_xml_string_view attribute_name =
      salts_xml_attribute_qualified_name(attribute);
  size_t count = salts_xml_node_child_count(node);
  size_t index;

  for (index = 0u; index < count; ++index) {
    salts_xml_node child = salts_xml_node_child_at(node, index);
    if (salts_xml_node_type(child) != SALTS_XML_ELEMENT) continue;
    if (xml_view_equal(salts_xml_node_display_name(child), attribute_name))
      return 1;
  }
  return 0;
}

static int xml_find_next_child(
    data_bind_xml_frame *frame,
    salts_xml_node *out) {
  size_t count = salts_xml_node_child_count(frame->node);
  while (frame->child_index < count) {
    salts_xml_node child =
        salts_xml_node_child_at(frame->node, frame->child_index++);
    if (salts_xml_node_type(child) == SALTS_XML_ELEMENT) {
      *out = child;
      return 1;
    }
  }
  return 0;
}

static int xml_find_next_attribute(
    data_bind_xml_frame *frame,
    salts_xml_attribute *out) {
  size_t count = salts_xml_node_attribute_count(frame->node);
  while (frame->attribute_index < count) {
    salts_xml_attribute attribute =
        salts_xml_node_attribute_at(frame->node, frame->attribute_index++);
    if (!xml_attribute_shadowed_by_child(frame->node, attribute)) {
      *out = attribute;
      return 1;
    }
  }
  return 0;
}

static cserde_status xml_emit_node(
    data_bind_xml_reader *context,
    salts_xml_node node,
    cserde_token *out) {
  size_t attribute_count = salts_xml_node_attribute_count(node);
  int has_element_child = xml_node_has_element_child(node);

  if (salts_xml_node_type(node) != SALTS_XML_ELEMENT)
    return CSERDE_UNSUPPORTED;

  if (!has_element_child && attribute_count == 0u) {
    xml_emit_slice(out, salts_xml_node_text_view(node));
    return CSERDE_OK;
  }

  if (context->depth >= context->max_depth)
    return CSERDE_LIMIT_EXCEEDED;

  {
    data_bind_xml_frame *frame = &context->frames[context->depth++];
    memset(frame, 0, sizeof(*frame));
    frame->node = node;
    frame->phase = DATA_BIND_XML_CHILD_KEY;
  }

  memset(out, 0, sizeof(*out));
  out->kind = CSERDE_MAP_BEGIN;
  return CSERDE_OK;
}

static cserde_status xml_provider_next(void *opaque, cserde_token *out) {
  data_bind_xml_reader *context = (data_bind_xml_reader *)opaque;

  if (context == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;

  if (context->selected_array && context->depth == 0u) {
    if (!context->array_started) {
      context->array_started = 1;
      memset(out, 0, sizeof(*out));
      out->kind = CSERDE_ARRAY_BEGIN;
      return CSERDE_OK;
    }
    if (context->selected_index <
        salts_xml_node_list_size(&context->selected)) {
      salts_xml_node node =
          salts_xml_node_list_at(&context->selected,
                                 context->selected_index++);
      return xml_emit_node(context, node, out);
    }
    if (!context->array_finished) {
      context->array_finished = 1;
      memset(out, 0, sizeof(*out));
      out->kind = CSERDE_ARRAY_END;
      return CSERDE_OK;
    }
    return CSERDE_DONE;
  }

  if (context->root_pending) {
    context->root_pending = 0;
    return xml_emit_node(context, context->root, out);
  }

  for (;;) {
    data_bind_xml_frame *frame;
    if (context->depth == 0u) return CSERDE_DONE;
    frame = &context->frames[context->depth - 1u];

    switch (frame->phase) {
    case DATA_BIND_XML_CHILD_KEY:
      if (xml_find_next_child(frame, &frame->pending_child)) {
        xml_emit_slice(out, salts_xml_node_display_name(frame->pending_child));
        frame->phase = DATA_BIND_XML_CHILD_VALUE;
        return CSERDE_OK;
      }
      frame->phase = DATA_BIND_XML_ATTRIBUTE_KEY;
      continue;

    case DATA_BIND_XML_CHILD_VALUE:
      frame->phase = DATA_BIND_XML_CHILD_KEY;
      return xml_emit_node(context, frame->pending_child, out);

    case DATA_BIND_XML_ATTRIBUTE_KEY:
      if (xml_find_next_attribute(frame, &frame->pending_attribute)) {
        xml_emit_slice(
            out, salts_xml_attribute_qualified_name(frame->pending_attribute));
        frame->phase = DATA_BIND_XML_ATTRIBUTE_VALUE;
        return CSERDE_OK;
      }
      frame->phase = DATA_BIND_XML_END;
      continue;

    case DATA_BIND_XML_ATTRIBUTE_VALUE:
      xml_emit_slice(
          out, salts_xml_attribute_value(frame->pending_attribute));
      frame->phase = DATA_BIND_XML_ATTRIBUTE_KEY;
      return CSERDE_OK;

    case DATA_BIND_XML_END:
      --context->depth;
      memset(out, 0, sizeof(*out));
      out->kind = CSERDE_MAP_END;
      return CSERDE_OK;
    }
  }
}

static const cserde_reader_ops XML_READER_OPS = {
    sizeof(cserde_reader_ops),
    CSERDE_READER_OPS_ABI_VERSION,
    xml_provider_next};

static data_bind_xml_reader *xml_context_create(
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindError *error) {
  data_bind_xml_reader *context;
  salts_xml_limits limits = salts_xml_default_limits();
  salts_xml_diagnostic diagnostic = {0};
  salts_xml_status status;
  size_t bytes;

  if (max_depth >
      (SIZE_MAX - sizeof(data_bind_xml_reader)) /
          sizeof(data_bind_xml_frame)) {
    xml_provider_error(error, DATA_BIND_ERR_LIMIT,
                       "XML provider depth is too large");
    return NULL;
  }

  bytes = sizeof(data_bind_xml_reader) +
          max_depth * sizeof(data_bind_xml_frame);
  context = (data_bind_xml_reader *)calloc(1u, bytes);
  if (context == NULL) {
    xml_provider_error(error, DATA_BIND_ERR_OOM,
                       "Unable to allocate XML provider state");
    return NULL;
  }

  if (max_depth != 0u && max_depth < limits.max_depth)
    limits.max_depth = max_depth;

  status = salts_xml_parse(
      &context->document, data, len, &limits, &diagnostic);
  if (status != SALTS_XML_OK) {
    DataBindStatus mapped =
        status == SALTS_XML_ALLOCATION_FAILED ? DATA_BIND_ERR_OOM :
        status == SALTS_XML_LIMIT_EXCEEDED ? DATA_BIND_ERR_LIMIT :
                                             DATA_BIND_ERR_PARSE;
    xml_provider_error(
        error, mapped,
        diagnostic.message[0] != '\0'
            ? diagnostic.message
            : "XML parse failed");
    free(context);
    return NULL;
  }

  context->root = salts_xml_document_root(&context->document);
  if (salts_xml_node_type(context->root) != SALTS_XML_ELEMENT) {
    salts_xml_document_destroy(&context->document);
    free(context);
    xml_provider_error(error, DATA_BIND_ERR_PARSE,
                       "XML document has no root element");
    return NULL;
  }
  context->max_depth = max_depth;
  return context;
}

static DataBindStatus xml_publish_context(
    data_bind_xml_reader *context,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  if (cserde_reader_init(&context->reader, &XML_READER_OPS, context) !=
      CSERDE_OK) {
    salts_xml_node_list_destroy(&context->selected);
    salts_xml_document_destroy(&context->document);
    free(context);
    return xml_provider_error(error, DATA_BIND_ERR_RUNTIME,
                              "Unable to initialize XML CSerde reader");
  }

  *out_reader = &context->reader;
  *out_owner = context;
  return DATA_BIND_OK;
}

static DataBindStatus xml_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  data_bind_xml_reader *context;

  if (out_reader == NULL || out_owner == NULL)
    return xml_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                              "Invalid XML provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  context = xml_context_create(data, len, max_depth, error);
  if (context == NULL)
    return error != NULL && error->code != DATA_BIND_OK
               ? error->code
               : DATA_BIND_ERR_PARSE;

  context->root_pending = 1;
  return xml_publish_context(context, out_reader, out_owner, error);
}

static DataBindStatus xml_provider_open_selected(
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindStreamSelection selection,
    const char *path,
    const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  data_bind_xml_reader *context;
  qvm_limits_t native_limits = xml_query_limits(query_limits);
  qvm_diagnostic_t native_diagnostic = {0};
  qvm_status_t query_status;

  if (out_reader == NULL || out_owner == NULL)
    return xml_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                              "Invalid selected XML provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  if (selection == DATA_BIND_STREAM_SELECT_ROOT)
    return xml_provider_open(
        data, len, max_depth, out_reader, out_owner, error);

  context = xml_context_create(data, len, max_depth, error);
  if (context == NULL)
    return error != NULL && error->code != DATA_BIND_OK
               ? error->code
               : DATA_BIND_ERR_PARSE;

  if (selection == DATA_BIND_STREAM_SELECT_ALL) {
    context->root_pending = 1;
    return xml_publish_context(context, out_reader, out_owner, error);
  }

  if (query_diagnostic != NULL) {
    size_t size = query_diagnostic->size;
    *query_diagnostic =
        (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
    query_diagnostic->size = size;
  }

  query_status = salts_xml_document_xpath_query(
      &context->document, path, &context->selected,
      &native_limits, &native_diagnostic);
  xml_query_diagnostic(query_diagnostic, &native_diagnostic);
  if (query_status != QVM_STATUS_OK) {
    DataBindStatus status = xml_query_failure(query_diagnostic);
    salts_xml_node_list_destroy(&context->selected);
    salts_xml_document_destroy(&context->document);
    free(context);
    return xml_provider_error(
        error, status,
        query_diagnostic != NULL && query_diagnostic->message[0] != '\0'
            ? query_diagnostic->message
            : "XPath query failed");
  }

  if (selection == DATA_BIND_STREAM_SELECT_PATH_FIRST) {
    if (salts_xml_node_list_size(&context->selected) == 0u) {
      salts_xml_node_list_destroy(&context->selected);
      salts_xml_document_destroy(&context->document);
      free(context);
      return xml_provider_error(error, DATA_BIND_ERR_TYPE_MISMATCH,
                                "XPath selected no value");
    }
    context->root =
        salts_xml_node_list_at(&context->selected, 0u);
    context->root_pending = 1;
  } else {
    context->selected_array = 1;
  }

  return xml_publish_context(context, out_reader, out_owner, error);
}

static void xml_provider_close(cserde_reader *reader, void *opaque) {
  data_bind_xml_reader *context = (data_bind_xml_reader *)opaque;
  (void)reader;
  if (context != NULL) {
    salts_xml_node_list_destroy(&context->selected);
    salts_xml_document_destroy(&context->document);
    free(context);
  }
}

static const DataBindFormatProvider XML_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_INIT(
        DATA_BIND_FORMAT_XML,
        xml_provider_open,
        xml_provider_close,
        xml_provider_open_selected);

const DataBindFormatProvider *data_bind_xml_format_provider(void) {
  return &XML_PROVIDER;
}

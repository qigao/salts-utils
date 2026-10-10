#include "data_bind_xml_provider.h"
#include "data_bind_projection_plan_internal.h"

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
  DATA_BIND_XML_END,
  DATA_BIND_XML_PLAN_KEY,
  DATA_BIND_XML_PLAN_VALUE,
  DATA_BIND_XML_PLAN_ITEMS
} data_bind_xml_phase;

typedef struct data_bind_xml_frame {
  salts_xml_node node;
  size_t child_index;
  size_t attribute_index;
  salts_xml_node pending_child;
  salts_xml_attribute pending_attribute;
  data_bind_xml_phase phase;
  uint32_t record;
  size_t field_index;
  DataBindXmlFieldPlan field;
} data_bind_xml_frame;

typedef struct data_bind_xml_reader {
  cserde_reader reader;
  const DataBindFormatPlan *plan;
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
  unsigned char *decoded;
  size_t decoded_capacity;
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

static int xml_entity_scalar_valid(uint32_t value) {
  return value == UINT32_C(0x09) || value == UINT32_C(0x0a) ||
         value == UINT32_C(0x0d) ||
         (value >= UINT32_C(0x20) && value <= UINT32_C(0xd7ff)) ||
         (value >= UINT32_C(0xe000) && value <= UINT32_C(0xfffd)) ||
         (value >= UINT32_C(0x10000) && value <= UINT32_C(0x10ffff));
}

static size_t xml_utf8_write(uint32_t value, unsigned char *out) {
  if (value <= UINT32_C(0x7f)) {
    out[0] = (unsigned char)value;
    return 1u;
  }
  if (value <= UINT32_C(0x7ff)) {
    out[0] = (unsigned char)(UINT32_C(0xc0) | (value >> 6));
    out[1] = (unsigned char)(UINT32_C(0x80) | (value & UINT32_C(0x3f)));
    return 2u;
  }
  if (value <= UINT32_C(0xffff)) {
    out[0] = (unsigned char)(UINT32_C(0xe0) | (value >> 12));
    out[1] = (unsigned char)(UINT32_C(0x80) |
                             ((value >> 6) & UINT32_C(0x3f)));
    out[2] = (unsigned char)(UINT32_C(0x80) | (value & UINT32_C(0x3f)));
    return 3u;
  }
  out[0] = (unsigned char)(UINT32_C(0xf0) | (value >> 18));
  out[1] = (unsigned char)(UINT32_C(0x80) |
                           ((value >> 12) & UINT32_C(0x3f)));
  out[2] = (unsigned char)(UINT32_C(0x80) |
                           ((value >> 6) & UINT32_C(0x3f)));
  out[3] = (unsigned char)(UINT32_C(0x80) | (value & UINT32_C(0x3f)));
  return 4u;
}

static int xml_entity_value(
    const char *text, size_t size, uint32_t *out) {
  size_t index;
  uint32_t value = 0u;
  unsigned base = 10u;

  if (text == NULL || out == NULL || size == 0u) return 0;
  if (size == 2u && memcmp(text, "lt", 2u) == 0) {
    *out = (uint32_t)'<';
    return 1;
  }
  if (size == 2u && memcmp(text, "gt", 2u) == 0) {
    *out = (uint32_t)'>';
    return 1;
  }
  if (size == 3u && memcmp(text, "amp", 3u) == 0) {
    *out = (uint32_t)'&';
    return 1;
  }
  if (size == 4u && memcmp(text, "quot", 4u) == 0) {
    *out = (uint32_t)'"';
    return 1;
  }
  if (size == 4u && memcmp(text, "apos", 4u) == 0) {
    *out = (uint32_t)'\'';
    return 1;
  }
  if (text[0] != '#' || size == 1u) return 0;

  index = 1u;
  if (index < size && (text[index] == 'x' || text[index] == 'X')) {
    base = 16u;
    ++index;
  }
  if (index == size) return 0;

  for (; index < size; ++index) {
    unsigned digit;
    unsigned char ch = (unsigned char)text[index];
    if (ch >= '0' && ch <= '9')
      digit = (unsigned)(ch - '0');
    else if (base == 16u && ch >= 'a' && ch <= 'f')
      digit = 10u + (unsigned)(ch - 'a');
    else if (base == 16u && ch >= 'A' && ch <= 'F')
      digit = 10u + (unsigned)(ch - 'A');
    else
      return 0;
    if (digit >= base ||
        value > (UINT32_C(0x10ffff) - digit) / base)
      return 0;
    value = value * base + digit;
  }
  if (!xml_entity_scalar_valid(value)) return 0;
  *out = value;
  return 1;
}

static cserde_status xml_emit_logical_slice(
    data_bind_xml_reader *context,
    cserde_token *out,
    salts_xml_string_view view,
    int decode_entities) {
  size_t input = 0u;
  size_t output = 0u;

  if (context == NULL || out == NULL ||
      (view.size != 0u && view.data == NULL))
    return CSERDE_INVALID_ARGUMENT;
  if (!decode_entities || view.size == 0u ||
      memchr(view.data, '&', view.size) == NULL) {
    xml_emit_slice(out, view);
    return CSERDE_OK;
  }
  if (view.size == SIZE_MAX || context->decoded == NULL ||
      context->decoded_capacity < view.size + 1u)
    return CSERDE_SOURCE_ERROR;

  while (input < view.size) {
    uint32_t value;
    size_t end;
    if (view.data[input] != '&') {
      context->decoded[output++] = (unsigned char)view.data[input++];
      continue;
    }

    end = input + 1u;
    while (end < view.size && view.data[end] != ';') ++end;
    if (end == view.size ||
        !xml_entity_value(
            view.data + input + 1u, end - input - 1u, &value))
      return CSERDE_SOURCE_ERROR;
    output += xml_utf8_write(value, context->decoded + output);
    input = end + 1u;
  }

  context->decoded[output] = 0u;
  memset(out, 0, sizeof(*out));
  out->kind = CSERDE_STRING;
  out->value.slice.data = context->decoded;
  out->value.slice.size = output;
  out->value.slice.lifetime = CSERDE_VIEW_TRANSIENT;
  return CSERDE_OK;
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

static salts_xml_node xml_node_first_text_child(salts_xml_node node) {
  salts_xml_node empty = {NULL};
  size_t count = salts_xml_node_child_count(node);
  size_t index;
  for (index = 0u; index < count; ++index) {
    salts_xml_node child = salts_xml_node_child_at(node, index);
    if (salts_xml_node_type(child) == SALTS_XML_TEXT)
      return child;
  }
  return empty;
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
  salts_xml_node_kind kind = salts_xml_node_type(node);
  size_t attribute_count;
  int has_element_child;

  if (kind == SALTS_XML_ATTRIBUTE)
    return xml_emit_logical_slice(
        context, out, salts_xml_node_text_view(node), 1);
  if (kind == SALTS_XML_TEXT)
    return xml_emit_logical_slice(
        context, out, salts_xml_node_text_view(node),
        salts_xml_node_text_has_entity_reference(node));
  if (kind != SALTS_XML_ELEMENT) return CSERDE_UNSUPPORTED;

  attribute_count = salts_xml_node_attribute_count(node);
  has_element_child = xml_node_has_element_child(node);

  if (!has_element_child && attribute_count == 0u) {
    salts_xml_node text = xml_node_first_text_child(node);
    if (salts_xml_node_type(text) == SALTS_XML_TEXT)
      return xml_emit_logical_slice(
          context, out, salts_xml_node_text_view(text),
          salts_xml_node_text_has_entity_reference(text));
    return xml_emit_logical_slice(
        context, out, salts_xml_node_text_view(node),
        salts_xml_node_text_has_entity_reference(node));
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

static size_t xml_plan_field_index(
    const data_bind_xml_reader *context, uint32_t record, salts_xml_string_view name) {
  return data_bind_format_record_field_find(context->plan, record, name.data, name.size);
}

static cserde_status xml_plan_emit_record(
    data_bind_xml_reader *context, salts_xml_node node, uint32_t record, cserde_token *out) {
  size_t i;
  data_bind_xml_frame *frame;
  if (context->depth == context->max_depth) return CSERDE_LIMIT_EXCEEDED;
  /* Validate the complete namespace before enumerating schema fields, so
   * schema-order grouping cannot silently discard unknown document keys. */
  for (i = 0u; i < salts_xml_node_child_count(node); ++i) {
    salts_xml_node child = salts_xml_node_child_at(node, i);
    if (salts_xml_node_type(child) == SALTS_XML_ELEMENT &&
        xml_plan_field_index(context, record, salts_xml_node_display_name(child)) == SIZE_MAX)
      return CSERDE_UNSUPPORTED;
  }
  for (i = 0u; i < salts_xml_node_attribute_count(node); ++i) {
    salts_xml_attribute attr = salts_xml_node_attribute_at(node, i);
    if (!xml_attribute_shadowed_by_child(node, attr) &&
        xml_plan_field_index(context, record, salts_xml_attribute_qualified_name(attr)) == SIZE_MAX)
      return CSERDE_UNSUPPORTED;
  }
  frame = &context->frames[context->depth++];
  memset(frame, 0, sizeof(*frame));
  frame->node = node;
  frame->record = record;
  frame->phase = DATA_BIND_XML_PLAN_KEY;
  *out = (cserde_token){.kind = CSERDE_MAP_BEGIN};
  return CSERDE_OK;
}

/* Search directly in the lease-owned DOM. No token buffering or retained
 * transient text is needed, even when repeated fields are interleaved. */
static int xml_plan_next_match(data_bind_xml_reader *context, data_bind_xml_frame *frame) {
  while (xml_find_next_child(frame, &frame->pending_child)) {
    if (xml_plan_field_index(context, frame->record,
            salts_xml_node_display_name(frame->pending_child)) == frame->field_index)
      return 1;
  }
  frame->pending_child = (salts_xml_node){0};
  while (xml_find_next_attribute(frame, &frame->pending_attribute)) {
    if (xml_plan_field_index(context, frame->record,
            salts_xml_attribute_qualified_name(frame->pending_attribute)) == frame->field_index)
      return 2;
  }
  frame->pending_attribute = (salts_xml_attribute){0};
  return 0;
}

static cserde_status xml_plan_emit_match(
    data_bind_xml_reader *context, const data_bind_xml_frame *frame, cserde_token *out) {
  if (frame->pending_child.impl != NULL) {
    if (frame->field.child != UINT32_MAX)
      return xml_plan_emit_record(context, frame->pending_child, frame->field.child, out);
    return xml_emit_node(context, frame->pending_child, out);
  }
  if (frame->field.child != UINT32_MAX || frame->field.sequence)
    return CSERDE_UNSUPPORTED;
  return xml_emit_logical_slice(context, out,
      salts_xml_attribute_value(frame->pending_attribute), 1);
}

static cserde_status xml_plan_next(data_bind_xml_reader *context, cserde_token *out) {
  if (context->root_pending) {
    context->root_pending = 0;
    return xml_plan_emit_record(context, context->root, 0u, out);
  }
  while (context->depth != 0u) {
    data_bind_xml_frame *frame = &context->frames[context->depth - 1u];
    if (frame->phase == DATA_BIND_XML_PLAN_ITEMS) {
      if (xml_plan_next_match(context, frame))
        return xml_plan_emit_match(context, frame, out);
      --context->depth;
      *out = (cserde_token){.kind = CSERDE_ARRAY_END};
      return CSERDE_OK;
    }
    if (frame->phase == DATA_BIND_XML_PLAN_VALUE) {
      frame->phase = DATA_BIND_XML_PLAN_KEY;
      if (frame->field.sequence) {
        data_bind_xml_frame *array;
        if (context->depth == context->max_depth) return CSERDE_LIMIT_EXCEEDED;
        array = &context->frames[context->depth++];
        *array = *frame;
        array->phase = DATA_BIND_XML_PLAN_ITEMS;
        array->child_index = array->attribute_index = 0u;
        ++frame->field_index;
        *out = (cserde_token){.kind = CSERDE_ARRAY_BEGIN};
        return CSERDE_OK;
      }
      {
        data_bind_xml_frame match = *frame;
        /* Scalar duplicate detection uses the same canonical identity as
         * arrays, including collisions between different alias spellings. */
        if (xml_plan_next_match(context, frame)) return CSERDE_UNSUPPORTED;
        ++frame->field_index;
        return xml_plan_emit_match(context, &match, out);
      }
    }
    if (frame->phase != DATA_BIND_XML_PLAN_KEY) return CSERDE_UNSUPPORTED;
    if (!data_bind_format_record_field_at(context->plan, frame->record,
                                         frame->field_index, &frame->field)) {
      --context->depth;
      *out = (cserde_token){.kind = CSERDE_MAP_END};
      return CSERDE_OK;
    }
    frame->child_index = frame->attribute_index = 0u;
    if (!xml_plan_next_match(context, frame) && !frame->field.sequence) {
      ++frame->field_index;
      continue;
    }
    frame->phase = DATA_BIND_XML_PLAN_VALUE;
    xml_emit_slice(out, (salts_xml_string_view){frame->field.name, strlen(frame->field.name)});
    return CSERDE_OK;
  }
  return CSERDE_DONE;
}

static cserde_status xml_provider_next(void *opaque, cserde_token *out) {
  data_bind_xml_reader *context = (data_bind_xml_reader *)opaque;

  if (context == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (context->plan != NULL) return xml_plan_next(context, out);

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

    case DATA_BIND_XML_ATTRIBUTE_VALUE: {
      cserde_status status = xml_emit_logical_slice(
          context, out, salts_xml_attribute_value(frame->pending_attribute), 1);
      frame->phase = DATA_BIND_XML_ATTRIBUTE_KEY;
      return status;
    }

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

  if (len != 0u && memchr(data, '&', len) != NULL) {
    if (len == SIZE_MAX) {
      free(context);
      xml_provider_error(error, DATA_BIND_ERR_LIMIT,
                         "XML logical text scratch size overflow");
      return NULL;
    }
    context->decoded = (unsigned char *)malloc(len + 1u);
    if (context->decoded == NULL) {
      free(context);
      xml_provider_error(error, DATA_BIND_ERR_OOM,
                         "Unable to allocate XML logical text scratch");
      return NULL;
    }
    context->decoded_capacity = len + 1u;
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
    free(context->decoded);
    free(context);
    return NULL;
  }

  context->root = salts_xml_document_root(&context->document);
  if (salts_xml_node_type(context->root) != SALTS_XML_ELEMENT) {
    salts_xml_document_destroy(&context->document);
    free(context->decoded);
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
    free(context->decoded);
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
    free(context->decoded);
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
      free(context->decoded);
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
    free(context->decoded);
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

DataBindStatus data_bind_xml_format_reader_open_plan(
    const DataBindFormatPlan *plan, const char *data, size_t len,
    size_t max_depth, DataBindFormatReader *out_reader, DataBindError *error) {
  DataBindStatus status;
  if (!data_bind_format_plan_xml_record(plan))
    return xml_provider_error(error, DATA_BIND_ERR_SCHEMA, "XML record FormatPlan required");
  status = data_bind_format_reader_open(&XML_PROVIDER, data, len, max_depth, out_reader, error);
  if (status == DATA_BIND_OK && data_bind_format_plan_has_sequences(plan))
    ((data_bind_xml_reader *)out_reader->owner)->plan = plan;
  return status;
}

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
  size_t max_depth;
  size_t depth;
  int root_pending;
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

static DataBindStatus xml_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  data_bind_xml_reader *context;
  salts_xml_limits limits = salts_xml_default_limits();
  salts_xml_diagnostic diagnostic = {0};
  salts_xml_status status;
  size_t bytes;

  if (out_reader == NULL || out_owner == NULL)
    return xml_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                              "Invalid XML provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  if (max_depth >
      (SIZE_MAX - sizeof(data_bind_xml_reader)) /
          sizeof(data_bind_xml_frame))
    return xml_provider_error(error, DATA_BIND_ERR_LIMIT,
                              "XML provider depth is too large");

  bytes = sizeof(data_bind_xml_reader) +
          max_depth * sizeof(data_bind_xml_frame);
  context = (data_bind_xml_reader *)calloc(1u, bytes);
  if (context == NULL)
    return xml_provider_error(error, DATA_BIND_ERR_OOM,
                              "Unable to allocate XML provider state");

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
    return mapped;
  }

  context->root = salts_xml_document_root(&context->document);
  if (salts_xml_node_type(context->root) != SALTS_XML_ELEMENT) {
    salts_xml_document_destroy(&context->document);
    free(context);
    return xml_provider_error(error, DATA_BIND_ERR_PARSE,
                              "XML document has no root element");
  }

  context->max_depth = max_depth;
  context->root_pending = 1;

  if (cserde_reader_init(&context->reader, &XML_READER_OPS, context) !=
      CSERDE_OK) {
    salts_xml_document_destroy(&context->document);
    free(context);
    return xml_provider_error(error, DATA_BIND_ERR_RUNTIME,
                              "Unable to initialize XML CSerde reader");
  }

  *out_reader = &context->reader;
  *out_owner = context;
  return DATA_BIND_OK;
}

static void xml_provider_close(cserde_reader *reader, void *opaque) {
  data_bind_xml_reader *context = (data_bind_xml_reader *)opaque;
  (void)reader;
  if (context != NULL) {
    salts_xml_document_destroy(&context->document);
    free(context);
  }
}

static const DataBindFormatProvider XML_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_INIT(
        DATA_BIND_FORMAT_XML,
        xml_provider_open,
        xml_provider_close);

const DataBindFormatProvider *data_bind_xml_format_provider(void) {
  return &XML_PROVIDER;
}

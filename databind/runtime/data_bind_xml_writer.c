#include "data_bind_xml_writer.h"

#include <xml_parser/xml_parser.h>
#include <tstr.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct data_bind_xml_writer_frame {
  salts_xml_node node;
  tstr pending_key;
  int sequence;
} data_bind_xml_writer_frame;

typedef struct data_bind_xml_writer_owner {
  cserde_writer writer;
  DataBindWriteFn write;
  void *write_user;
  salts_xml_document document;
  salts_xml_node root;
  size_t depth;
  size_t max_depth;
  int root_started;
  int complete;
  data_bind_xml_writer_frame frames[];
} data_bind_xml_writer_owner;

static DataBindStatus xml_writer_error(
    DataBindError *error, DataBindStatus status, const char *message) {
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

static cserde_status xml_writer_status(salts_xml_status status) {
  switch (status) {
  case SALTS_XML_OK:
    return CSERDE_OK;
  case SALTS_XML_ALLOCATION_FAILED:
    return CSERDE_CALLBACK_ERROR;
  case SALTS_XML_LIMIT_EXCEEDED:
    return CSERDE_LIMIT_EXCEEDED;
  case SALTS_XML_INVALID_ARGUMENT:
  case SALTS_XML_EMBEDDED_NUL:
  case SALTS_XML_UNSUPPORTED:
  case SALTS_XML_MALFORMED:
  default:
    return CSERDE_UNSUPPORTED;
  }
}

static int xml_writer_name_valid_bytes(
    const unsigned char *data, size_t size) {
  size_t index;
  unsigned char ch;
  if (data == NULL || size == 0u) return 0;
  ch = data[0];
  if (!((ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') || ch == '_'))
    return 0;
  for (index = 1u; index < size; ++index) {
    ch = data[index];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_' || ch == '-' || ch == '.'))
      return 0;
  }
  return 1;
}

static int xml_writer_name_valid(const char *name) {
  return name != NULL &&
         xml_writer_name_valid_bytes(
             (const unsigned char *)name, strlen(name));
}

static cserde_status xml_writer_copy_slice(
    const cserde_token *token, char **out) {
  char *copy;
  if (out == NULL) return CSERDE_INVALID_ARGUMENT;
  *out = NULL;
  if (token == NULL || token->kind != CSERDE_STRING ||
      (token->value.slice.size != 0u &&
       token->value.slice.data == NULL))
    return CSERDE_UNSUPPORTED;
  if (token->value.slice.size == SIZE_MAX)
    return CSERDE_LIMIT_EXCEEDED;
  if (token->value.slice.size != 0u &&
      memchr(token->value.slice.data, '\0',
             token->value.slice.size) != NULL)
    return CSERDE_UNSUPPORTED;
  copy = (char *)malloc(token->value.slice.size + 1u);
  if (copy == NULL) return CSERDE_CALLBACK_ERROR;
  if (token->value.slice.size != 0u)
    memcpy(copy, token->value.slice.data, token->value.slice.size);
  copy[token->value.slice.size] = '\0';
  *out = copy;
  return CSERDE_OK;
}

static cserde_status xml_writer_scalar_text(
    const cserde_token *token, char **out) {
  char buffer[96];
  int written;
  char *copy;

  if (out == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  *out = NULL;

  switch (token->kind) {
  case CSERDE_STRING:
    return xml_writer_copy_slice(token, out);
  case CSERDE_BOOL:
    copy = (char *)malloc(token->value.boolean ? 5u : 6u);
    if (copy == NULL) return CSERDE_CALLBACK_ERROR;
    memcpy(copy, token->value.boolean ? "true" : "false",
           token->value.boolean ? 5u : 6u);
    *out = copy;
    return CSERDE_OK;
  case CSERDE_SINT:
    written = snprintf(buffer, sizeof(buffer), "%" PRId64,
                       token->value.sint);
    break;
  case CSERDE_UINT:
    written = snprintf(buffer, sizeof(buffer), "%" PRIu64,
                       token->value.uint);
    break;
  case CSERDE_FLOAT:
    if (!isfinite(token->value.floating)) return CSERDE_UNSUPPORTED;
    written = snprintf(buffer, sizeof(buffer), "%.17g",
                       token->value.floating);
    break;
  default:
    return CSERDE_UNSUPPORTED;
  }

  if (written < 0 || (size_t)written >= sizeof(buffer))
    return CSERDE_UNSUPPORTED;
  copy = (char *)malloc((size_t)written + 1u);
  if (copy == NULL) return CSERDE_CALLBACK_ERROR;
  memcpy(copy, buffer, (size_t)written + 1u);
  *out = copy;
  return CSERDE_OK;
}

static cserde_status xml_root_writer_write(
    void *opaque, const cserde_token *token) {
  data_bind_xml_writer_owner *owner =
      (data_bind_xml_writer_owner *)opaque;
  salts_xml_node child = {0};
  cserde_status status;
  salts_xml_status xml_status;
  char *text = NULL;
  data_bind_xml_writer_frame *frame;

  if (owner == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  if (owner->complete) return CSERDE_UNSUPPORTED;

  if (!owner->root_started) {
    if (token->kind != CSERDE_MAP_BEGIN) return CSERDE_UNSUPPORTED;
    owner->root_started = 1;
    owner->frames[owner->depth++].node = owner->root;
    return CSERDE_OK;
  }

  frame = &owner->frames[owner->depth - 1u];
  if (frame->sequence && token->kind == CSERDE_ARRAY_END) {
    tstr_freep(&frame->pending_key);
    --owner->depth;
    return CSERDE_OK;
  }
  if (!frame->sequence && frame->pending_key == NULL) {
    if (token->kind == CSERDE_MAP_END) {
      --owner->depth;
      owner->complete = owner->depth == 0u;
      return CSERDE_OK;
    }
    if (token->kind != CSERDE_STRING ||
        !xml_writer_name_valid_bytes(
            token->value.slice.data, token->value.slice.size))
      return CSERDE_UNSUPPORTED;
    frame->pending_key = tstr_dup_len(
        (const char *)token->value.slice.data, token->value.slice.size);
    return frame->pending_key != NULL ? CSERDE_OK : CSERDE_CALLBACK_ERROR;
  }

  if (token->kind == CSERDE_ARRAY_BEGIN) {
    data_bind_xml_writer_frame *array;
    if (frame->sequence) return CSERDE_UNSUPPORTED;
    if (owner->depth == owner->max_depth) return CSERDE_LIMIT_EXCEEDED;
    array = &owner->frames[owner->depth++];
    *array = (data_bind_xml_writer_frame){frame->node, frame->pending_key, 1};
    frame->pending_key = NULL;
    return CSERDE_OK;
  }
  if (token->kind == CSERDE_MAP_BEGIN) {
    if (owner->depth == owner->max_depth) return CSERDE_LIMIT_EXCEEDED;
    xml_status = salts_xml_node_add_element(
        frame->node, frame->pending_key, &child);
    if (xml_status != SALTS_XML_OK) return xml_writer_status(xml_status);
    if (!frame->sequence) tstr_freep(&frame->pending_key);
    owner->frames[owner->depth++] = (data_bind_xml_writer_frame){child, NULL, 0};
    return CSERDE_OK;
  }

  status = xml_writer_scalar_text(token, &text);
  if (status != CSERDE_OK) return status;

  xml_status = salts_xml_node_add_element(
      frame->node, frame->pending_key, &child);
  if (xml_status == SALTS_XML_OK)
    xml_status = salts_xml_node_set_text(child, text);
  free(text);
  if (!frame->sequence) tstr_freep(&frame->pending_key);
  return xml_writer_status(xml_status);
}

static cserde_status xml_root_writer_finish(void *opaque) {
  data_bind_xml_writer_owner *owner =
      (data_bind_xml_writer_owner *)opaque;
  char *output;
  size_t output_len = 0u;
  int write_status;

  if (owner == NULL || owner->write == NULL ||
      !owner->root_started || !owner->complete ||
      owner->depth != 0u)
    return CSERDE_UNSUPPORTED;

  output = salts_xml_document_serialize(
      &owner->document, &output_len);
  if (output == NULL) return CSERDE_CALLBACK_ERROR;
  write_status = owner->write(output, output_len, owner->write_user);
  salts_xml_owned_string_free(output);
  return write_status == 0 ? CSERDE_OK : CSERDE_SINK_ERROR;
}

static const cserde_writer_ops XML_ROOT_WRITER_OPS = {
    sizeof(cserde_writer_ops),
    CSERDE_WRITER_OPS_ABI_VERSION,
    xml_root_writer_write,
    xml_root_writer_finish};

static DataBindStatus xml_writer_result(
    cserde_status status, DataBindError *error) {
  switch (status) {
  case CSERDE_OK:
    return xml_writer_error(error, DATA_BIND_OK, "");
  case CSERDE_LIMIT_EXCEEDED:
    return xml_writer_error(
        error, DATA_BIND_ERR_LIMIT,
        "XML writer exceeded its configured limits");
  case CSERDE_UNSUPPORTED:
    return xml_writer_error(
        error, DATA_BIND_ERR_TYPE_MISMATCH,
        "Canonical token stream is not representable by the XML record writer");
  case CSERDE_SINK_ERROR:
    return xml_writer_error(
        error, DATA_BIND_ERR_IO,
        "XML writer byte sink rejected output");
  case CSERDE_CALLBACK_ERROR:
    return xml_writer_error(
        error, DATA_BIND_ERR_OOM,
        "XML writer could not build or emit the document");
  default:
    return xml_writer_error(
        error, DATA_BIND_ERR_RUNTIME,
        "XML CSerde writer failed");
  }
}

DataBindStatus data_bind_xml_writer_open_root(
    const char *root_name,
    DataBindWriteFn write,
    void *write_user,
    size_t max_depth,
    DataBindXmlWriter *out_writer,
    DataBindError *error) {
  data_bind_xml_writer_owner *owner;
  salts_xml_status xml_status;
  size_t out_size;

  if (out_writer == NULL || out_writer->size < sizeof(*out_writer))
    return xml_writer_error(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Invalid XML writer output");
  out_size = out_writer->size;
  memset(out_writer, 0, sizeof(*out_writer));
  out_writer->size = out_size;

  if (!xml_writer_name_valid(root_name) || write == NULL)
    return xml_writer_error(
        error, DATA_BIND_ERR_INVALID_ARG,
        "XML writer requires a valid namespace-free root name and byte sink");
  if (max_depth < 1u ||
      max_depth > (SIZE_MAX - sizeof(*owner)) / sizeof(owner->frames[0]))
    return xml_writer_error(
        error, DATA_BIND_ERR_LIMIT,
        "XML writer depth is zero or exceeds addressable capacity");

  owner = (data_bind_xml_writer_owner *)calloc(
      1u, sizeof(*owner) + max_depth * sizeof(owner->frames[0]));
  if (owner == NULL)
    return xml_writer_error(
        error, DATA_BIND_ERR_OOM,
        "Unable to allocate XML writer lease");

  owner->write = write;
  owner->max_depth = max_depth;
  owner->write_user = write_user;
  xml_status = salts_xml_document_create(&owner->document, root_name);
  if (xml_status != SALTS_XML_OK) {
    free(owner);
    return xml_writer_error(
        error,
        xml_status == SALTS_XML_ALLOCATION_FAILED
            ? DATA_BIND_ERR_OOM
            : DATA_BIND_ERR_INVALID_ARG,
        "Unable to create XML writer root element");
  }
  owner->root = salts_xml_document_root(&owner->document);

  if (cserde_writer_init(
          &owner->writer, &XML_ROOT_WRITER_OPS, owner) != CSERDE_OK) {
    salts_xml_document_destroy(&owner->document);
    free(owner);
    return xml_writer_error(
        error, DATA_BIND_ERR_RUNTIME,
        "Unable to initialize XML CSerde writer");
  }

  out_writer->writer = &owner->writer;
  out_writer->owner = owner;
  return xml_writer_error(error, DATA_BIND_OK, "");
}

cserde_writer *data_bind_xml_writer_writer(DataBindXmlWriter *writer) {
  if (writer == NULL || writer->size < sizeof(*writer) ||
      writer->owner == NULL || writer->writer == NULL ||
      writer->writer->state == CSERDE_WRITER_ZERO)
    return NULL;
  return writer->writer;
}

DataBindStatus data_bind_xml_writer_close(
    DataBindXmlWriter *writer,
    DataBindError *error) {
  data_bind_xml_writer_owner *owner;
  cserde_status status;
  size_t size;

  if (writer == NULL || writer->size < sizeof(*writer))
    return xml_writer_error(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Invalid XML writer lease");

  size = writer->size;
  owner = (data_bind_xml_writer_owner *)writer->owner;
  status = writer->writer != NULL
               ? cserde_writer_finish(writer->writer)
               : CSERDE_INVALID_ARGUMENT;

  if (owner != NULL) {
    size_t i;
    for (i = 0u; i < owner->depth; ++i)
      tstr_freep(&owner->frames[i].pending_key);
    salts_xml_document_destroy(&owner->document);
    free(owner);
  }

  memset(writer, 0, sizeof(*writer));
  writer->size = size;
  return xml_writer_result(status, error);
}

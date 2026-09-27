#include "flowmq_plan_projection.h"

#include "binary_layout_ir.h"
#include "binary_reader_codegen.h"
#include "message_native.h"

#include "salts_fs.h"
#include "salts_uuid.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node *flowmq_child(const Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP || name == NULL) return NULL;
  for (i = 0u; i < parent->data.map.count; ++i) {
    Node *child = parent->data.map.items[i];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static const char *flowmq_string(const Node *parent, const char *name) {
  Node *child = flowmq_child(parent, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

static const Node *flowmq_find_channel(
    const Node *root, const char *qualified_name) {
  Node *channels = flowmq_child(root, "channels");
  size_t i;
  if (channels == NULL || channels->type != NODE_LIST ||
      qualified_name == NULL || qualified_name[0] == '\0')
    return NULL;
  for (i = 0u; i < channels->data.list.count; ++i) {
    const Node *channel = channels->data.list.items[i];
    const char *candidate = flowmq_string(channel, "qualified_name");
    if (candidate != NULL && strcmp(candidate, qualified_name) == 0)
      return channel;
  }
  return NULL;
}

static int flowmq_identifier_valid(const char *text) {
  size_t i;
  if (text == NULL || text[0] == '\0' ||
      !((text[0] >= 'A' && text[0] <= 'Z') ||
        (text[0] >= 'a' && text[0] <= 'z') || text[0] == '_'))
    return 0;
  for (i = 1u; text[i] != '\0'; ++i)
    if (!((text[i] >= 'A' && text[i] <= 'Z') ||
          (text[i] >= 'a' && text[i] <= 'z') ||
          (text[i] >= '0' && text[i] <= '9') || text[i] == '_'))
      return 0;
  return 1;
}

static int flowmq_include_basename_valid(const char *text) {
  size_t i;
  if (text == NULL || text[0] == '\0' ||
      strcmp(text, ".") == 0 || strcmp(text, "..") == 0)
    return 0;
  for (i = 0u; text[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_' || ch == '-' || ch == '.'))
      return 0;
  }
  return 1;
}

static int flowmq_c_string(FILE *file, const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (file == NULL || text == NULL || fputc('"', file) == EOF) return -1;
  for (; *p != '\0'; ++p) {
    if (*p == '\\' || *p == '"') {
      if (fputc('\\', file) == EOF || fputc((int)*p, file) == EOF)
        return -1;
    } else if (*p < 0x20u || *p >= 0x7fu) {
      if (fprintf(file, "\\x%02X", (unsigned)*p) < 0) return -1;
    } else if (fputc((int)*p, file) == EOF) {
      return -1;
    }
  }
  return fputc('"', file) == EOF ? -1 : 0;
}

static const char *flowmq_format_name(DataBindFormat format) {
  switch (format) {
  case DATA_BIND_FORMAT_JSON:
    return "DATA_BIND_FORMAT_JSON";
  case DATA_BIND_FORMAT_BINARY:
    return "DATA_BIND_FORMAT_BINARY";
  default:
    return NULL;
  }
}

static const char *flowmq_pattern_name(
    DataBindFlowMQChannelPattern pattern) {
  switch (pattern) {
  case DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB:
    return "DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB";
  case DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL:
    return "DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL";
  default:
    return NULL;
  }
}

static int flowmq_config_valid(
    const databind_compiler_flowmq_projection_config *config) {
  return config != NULL &&
      flowmq_identifier_valid(config->symbol_prefix) &&
      flowmq_include_basename_valid(config->native_header_include) &&
      config->channel_name != NULL && config->channel_name[0] != '\0' &&
      flowmq_format_name(config->format) != NULL &&
      flowmq_pattern_name(config->pattern) != NULL &&
      config->max_payload_bytes != 0u &&
      config->max_payload_bytes <= UINT32_MAX;
}

static int flowmq_format_representable(
    const Node *root, const char *message_type, DataBindFormat format) {
  databind_binary_type_layout layout = {0};
  databind_binary_layout_diagnostic diagnostic = {0};
  databind_binary_layout_status status;

  if (format == DATA_BIND_FORMAT_JSON)
    return message_type != NULL && message_type[0] != '\0';
  if (format != DATA_BIND_FORMAT_BINARY) return 0;

  status = databind_binary_layout_build(
      root, message_type, &layout, &diagnostic);
  databind_binary_layout_destroy(&layout);
  return status == DATABIND_BINARY_LAYOUT_OK;
}

static int flowmq_open_atomic(
    const char *output, char **out_temp, FILE **out_file) {
  salts_uuid_t uuid;
  char uuid_text[SALTS_UUID_STRING_SIZE];
  size_t length;
  char *temp;
  FILE *file;

  if (output == NULL || output[0] == '\0' ||
      out_temp == NULL || out_file == NULL)
    return -1;
  *out_temp = NULL;
  *out_file = NULL;
  if (salts_uuid_v4_generate(&uuid) != SALTS_OK ||
      salts_uuid_format(&uuid, uuid_text, sizeof(uuid_text)) != SALTS_OK)
    return -1;
  length = strlen(output);
  if (length > SIZE_MAX - strlen(uuid_text) - 7u) return -1;
  temp = (char *)malloc(length + strlen(uuid_text) + 7u);
  if (temp == NULL) return -1;
  snprintf(temp, length + strlen(uuid_text) + 7u, "%s.%s.tmp",
           output, uuid_text);
  file = fopen(temp, "wb");
  if (file == NULL) {
    free(temp);
    return -1;
  }
  *out_temp = temp;
  *out_file = file;
  return 0;
}

static int flowmq_commit_atomic(
    const char *output, char *temp, FILE *file, int success) {
  int result = -1;
  if (file != NULL && fclose(file) != 0) success = 0;
  if (success && salts_fs_rename(temp, output) == SALTS_OK) result = 0;
  if (result != 0 && temp != NULL) (void)salts_fs_unlink(temp);
  free(temp);
  return result;
}

static int flowmq_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  const Node *root = input != NULL ? input->legacy_tree : NULL;
  const databind_compiler_flowmq_projection_config *config =
      request != NULL
          ? (const databind_compiler_flowmq_projection_config *)request->config
          : NULL;
  const Node *channel;
  const char *message_type;
  databind_compiler_message_native_binding native_binding = {0};
  char native_symbol[384];
  char *temp = NULL;
  FILE *file = NULL;
  int ok = 0;
  (void)context;

  if (root == NULL || request == NULL || request->output == NULL ||
      request->id.axis != DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT ||
      request->id.kind != DATABIND_COMPILER_TRANSPORT_FLOWMQ ||
      !flowmq_config_valid(config))
    return -1;

  channel = flowmq_find_channel(root, config->channel_name);
  if (channel == NULL) return -1;
  message_type = flowmq_string(channel, "message_type");
  if (!flowmq_format_representable(root, message_type, config->format))
    return -1;
  if (databind_compiler_message_native_build(
          input->contract, message_type, &native_binding) != 0)
    return -1;
  if (snprintf(
          native_symbol, sizeof(native_symbol), "%s_flowmq",
          config->symbol_prefix) <= 0 ||
      strlen(native_symbol) >= sizeof(native_symbol) - 1u) {
    databind_compiler_message_native_destroy(&native_binding);
    return -1;
  }

  if (flowmq_open_atomic(request->output, &temp, &file) != 0) {
    databind_compiler_message_native_destroy(&native_binding);
    return -1;
  }

  if (fprintf(file,
              "#ifndef DATABIND_GENERATED_%s_FLOWMQ_PLAN_H\n"
              "#define DATABIND_GENERATED_%s_FLOWMQ_PLAN_H\n\n"
              "#include <data_bind_flowmq_plan.h>\n"
              "#include \"%s\"\n\n",
              config->symbol_prefix,
              config->symbol_prefix,
              config->native_header_include) < 0)
    goto cleanup;

  if (databind_compiler_message_native_emit_binding(
          file, &native_binding, native_symbol) != 0)
    goto cleanup;

  if (config->format == DATA_BIND_FORMAT_BINARY &&
      databind_compiler_binary_reader_admit(root, message_type) == 0) {
    if (fputc('\n', file) == EOF ||
        databind_compiler_binary_reader_emit(
            file, root, message_type, config->symbol_prefix) != 0)
      goto cleanup;
  }

  if (fprintf(
          file,
          "\nstatic const DataBindFlowMQChannelPlan %s_flowmq_channel_plan = {\n"
          "  sizeof(DataBindFlowMQChannelPlan), "
          "DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION,\n"
          "  ",
          config->symbol_prefix) < 0 ||
      flowmq_c_string(file, config->channel_name) != 0 ||
      fputs(",\n  ", file) == EOF ||
      flowmq_c_string(file, message_type) != 0 ||
      fprintf(
          file,
          ",\n  %s, %s, %zuu,\n"
          "  %s__databind_message_native_binding\n"
          "};\n\n"
          "#endif /* DATABIND_GENERATED_%s_FLOWMQ_PLAN_H */\n",
          flowmq_format_name(config->format),
          flowmq_pattern_name(config->pattern),
          config->max_payload_bytes,
          native_symbol,
          config->symbol_prefix) < 0)
    goto cleanup;

  ok = 1;

cleanup:
  databind_compiler_message_native_destroy(&native_binding);
  return flowmq_commit_atomic(request->output, temp, file, ok);
}

databind_compiler_projection_backend
databind_compiler_flowmq_channel_plan_backend(void) {
  databind_compiler_projection_backend backend = {
      {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
       DATABIND_COMPILER_TRANSPORT_FLOWMQ},
      "flowmq",
      flowmq_generate,
      NULL};
  return backend;
}

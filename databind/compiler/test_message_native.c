#include "compiler_core.h"
#include "message_native.h"

#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MESSAGE_NATIVE_SCHEMA
#error "MESSAGE_NATIVE_SCHEMA is required"
#endif

static int emitted_contains(
    const databind_compiler_message_native_binding *binding,
    const char *needle) {
  FILE *file = tmpfile();
  char buffer[8192];
  size_t length;
  int found = 0;

  if (file == NULL) return 0;
  if (databind_compiler_message_native_emit_binding(
          file, binding, "databind_native_channel_event") != 0) {
    fclose(file);
    return 0;
  }
  if (fflush(file) != 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }
  length = fread(buffer, 1u, sizeof(buffer) - 1u, file);
  buffer[length] = '\0';
  found = strstr(buffer, needle) != NULL;
  fclose(file);
  return found;
}

spec("DataBind reusable message-native lowering") {
  it("builds one Channel payload without a fake Service") {
    Node *root = NULL;
    char *schema_data = NULL;
    databind_compiler_message_native_binding binding = {0};

    check_equal(
        tbe_compiler_parse_schema_file(
            MESSAGE_NATIVE_SCHEMA, &root, &schema_data),
        0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || schema_data == NULL) {
      node_free(root);
      free(schema_data);
      return;
    }

    check_equal(
        databind_compiler_message_native_build(
            root, "Event", &binding),
        0);
    check_equal(binding.schema_name, "NativeChannel");
    check_equal(binding.type_name, "Event");
    check_equal(binding.type_identity,
                "tbe.native.NativeChannel.Event_t");

    check_equal(binding.presence_count, (size_t)2u);
    check_equal(binding.presence[0].field_name, "sequence");
    check_equal(binding.presence[0].bit, 0u);
    check_equal(binding.presence[1].field_name, "tri");
    check_equal(binding.presence[1].bit, 1u);

    check_equal(binding.null_count, (size_t)2u);
    check_equal(binding.nulls[0].field_name, "result");
    check_equal(binding.nulls[0].bit, 0u);
    check_equal(binding.nulls[1].field_name, "tri");
    check_equal(binding.nulls[1].bit, 1u);

    check_true(emitted_contains(
        &binding, "Event_cmeta_data(&data, error)"));
    check_true(emitted_contains(
        &binding, "offsetof(Event_t, _presence)"));
    check_true(emitted_contains(
        &binding, "offsetof(Event_t, _nulls)"));
    check_true(emitted_contains(
        &binding, "DATA_BIND_NATIVE_BINDING_ABI_VERSION"));
    check_true(emitted_contains(
        &binding,
        "databind_native_channel_event__databind_message_native_binding"));

    databind_compiler_message_native_destroy(&binding);
    node_free(root);
    free(schema_data);
  }

  it("fails closed for an unknown or non-message type") {
    Node *root = NULL;
    char *schema_data = NULL;
    databind_compiler_message_native_binding binding = {0};

    check_equal(
        tbe_compiler_parse_schema_file(
            MESSAGE_NATIVE_SCHEMA, &root, &schema_data),
        0);
    check_not_null(root);
    if (root == NULL) {
      free(schema_data);
      return;
    }

    check_equal(
        databind_compiler_message_native_build(
            root, "Missing", &binding),
        -1);
    check_null(binding.type_name);

    databind_compiler_message_native_destroy(&binding);
    node_free(root);
    free(schema_data);
  }
}

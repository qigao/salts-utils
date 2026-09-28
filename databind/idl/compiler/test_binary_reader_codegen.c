#include "binary_reader_codegen.h"
#include "compiler_core.h"
#include "tbe_contract_overlay.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BINARY_SCALAR_SCHEMA
#error "BINARY_SCALAR_SCHEMA is required"
#endif

#ifndef SCHEMA_EXAMPLE_FILE
#error "SCHEMA_EXAMPLE_FILE is required"
#endif

static char *emit_to_text(
    const IdlContract *contract,
    const databind_tbe_format_plan *format_plan,
    const char *type_name,
    const char *prefix) {
  FILE *file = tmpfile();
  char *text = NULL;
  long size;

  if (file == NULL) return NULL;
  if (databind_compiler_binary_reader_emit(
          file, contract, format_plan, type_name, prefix) != 0) {
    fclose(file);
    return NULL;
  }
  if (fflush(file) != 0 || fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  text = (char *)malloc((size_t)size + 1u);
  if (text == NULL) {
    fclose(file);
    return NULL;
  }
  if ((size_t)size != 0u &&
      fread(text, 1u, (size_t)size, file) != (size_t)size) {
    free(text);
    fclose(file);
    return NULL;
  }
  text[size] = '\0';
  fclose(file);
  return text;
}

spec("DataBind compiler Binary reader codegen") {
  it("lowers canonical scalar BinaryLayoutIR to one type-level provider") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_tbe_format_plan format_plan = {0};
    tbe_error_t format_error;
    char *text = NULL;

    check_equal(
        databind_compiler_parse_contract_file(
            BINARY_SCALAR_SCHEMA, &root, &contract, &schema_data),
        0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || schema_data == NULL) {
      node_free(root);
      free(schema_data);
      return;
    }

    tbe_error_init(&format_error);
    check(databind_tbe_format_plan_build(
        contract, root, &format_plan, &format_error));
    check_equal(
        databind_compiler_binary_reader_admit(
            contract, &format_plan, "Scalars"),
        0);

    text = emit_to_text(
        contract, &format_plan, "Scalars", "databind_binary_fixture");
    check_not_null(text);
    if (text != NULL) {
      check_contains(
          text,
          "DATABIND_GENERATED_databind_binary_fixture_binary_Scalars_READER_INCLUDED");
      check_contains(text, "#include <data_bind_binary_reader.h>");
      check_contains(text, "\"ready\", CSERDE_BOOL, 8u");
      check_contains(text, "\"delta8\", CSERDE_SINT, 8u");
      check_contains(text, "\"count16\", CSERDE_UINT, 16u");
      check_contains(text, "\"ratio\", CSERDE_FLOAT, 32u");
      check_contains(text, "\"score\", CSERDE_FLOAT, 64u");
      check_contains(text, "\"code\", CSERDE_SINT, 16u");
      check_contains(text, "\"perms\", CSERDE_UINT, 8u");
      check_contains(
          text,
          "DATA_BIND_BINARY_READER_FIELD_OPTIONAL | "
          "DATA_BIND_BINARY_READER_FIELD_NULLABLE");
      check_contains(
          text,
          "databind_binary_fixture_binary_Scalars_databind_binary_provider");
      check_contains(
          text,
          "data_bind_binary_reader_open(");
      check_contains(
          text,
          "DATA_BIND_FORMAT_PROVIDER_INIT(");
    }

    free(text);
    databind_tbe_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("fails closed when BinaryLayoutIR contains unsupported tail semantics") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_tbe_format_plan format_plan = {0};
    tbe_error_t format_error;
    FILE *file = tmpfile();

    check_equal(
        databind_compiler_parse_contract_file(
            SCHEMA_EXAMPLE_FILE, &root, &contract, &schema_data),
        0);
    check_not_null(root);
    check_not_null(schema_data);
    check_not_null(file);

    if (root != NULL && file != NULL) {
      tbe_error_init(&format_error);
      check(databind_tbe_format_plan_build(
          contract, root, &format_plan, &format_error));
      check_equal(
          databind_compiler_binary_reader_admit(
              contract, &format_plan, "LoginMessage"),
          -1);
      check_equal(
          databind_compiler_binary_reader_emit(
              file, contract, &format_plan, "LoginMessage", "databind_login"),
          -1);
    }

    if (file != NULL) fclose(file);
    databind_tbe_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("rejects invalid generated symbol identity before writing output") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_tbe_format_plan format_plan = {0};
    tbe_error_t format_error;
    FILE *file = tmpfile();

    check_equal(
        databind_compiler_parse_contract_file(
            BINARY_SCALAR_SCHEMA, &root, &contract, &schema_data),
        0);
    check_not_null(root);
    check_not_null(file);

    if (root != NULL && file != NULL) {
      tbe_error_init(&format_error);
      check(databind_tbe_format_plan_build(
          contract, root, &format_plan, &format_error));
      check_equal(
          databind_compiler_binary_reader_emit(
              file, contract, &format_plan, "Scalars", "bad-prefix"),
          -1);
    }

    if (file != NULL) fclose(file);
    databind_tbe_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }
}

#include "compiler_core.h"
#include "plugin_projection.h"
#include "projection.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PLUGIN_PROJECTION_SCHEMA_FILE
#error "PLUGIN_PROJECTION_SCHEMA_FILE is required"
#endif
#ifndef PLUGIN_PROJECTION_NO_VERSION_SCHEMA_FILE
#error "PLUGIN_PROJECTION_NO_VERSION_SCHEMA_FILE is required"
#endif
#ifndef PLUGIN_PROJECTION_OUTPUT_FILE
#error "PLUGIN_PROJECTION_OUTPUT_FILE is required"
#endif
#ifndef PLUGIN_PROJECTION_INVALID_OUTPUT_FILE
#error "PLUGIN_PROJECTION_INVALID_OUTPUT_FILE is required"
#endif
#ifndef PLUGIN_NATIVE_HEADER_OUTPUT_FILE
#error "PLUGIN_NATIVE_HEADER_OUTPUT_FILE is required"
#endif
#ifndef PLUGIN_NATIVE_SOURCE_OUTPUT_FILE
#error "PLUGIN_NATIVE_SOURCE_OUTPUT_FILE is required"
#endif

static int text_contains(const char *text, const char *needle) {
  return text != NULL && needle != NULL && strstr(text, needle) != NULL;
}

static int write_text(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  size_t length = text != NULL ? strlen(text) : 0u;
  int ok;
  if (file == NULL) return 0;
  ok = fwrite(text, 1u, length, file) == length &&
       fflush(file) == 0 && fclose(file) == 0;
  return ok;
}

static int file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return 0;
  fclose(file);
  return 1;
}

static int generate_native_header(void) {
  tbe_compiler_options_t options = {
      .schema_path = PLUGIN_PROJECTION_SCHEMA_FILE,
      .output_path = PLUGIN_NATIVE_HEADER_OUTPUT_FILE,
      .source_output_path = PLUGIN_NATIVE_SOURCE_OUTPUT_FILE,
      .resource_dir = TBE_COMPILER_RESOURCE_DIR,
      .lang_enum = TBE_COMPILER_LANG_C,
  };
  return tbe_compiler_run(&options);
}

static int generate_plugin(
    const char *schema_path, const char *output_path,
    uint32_t major, uint32_t minor, uint32_t patch) {
  Node *root = NULL;
  char *schema_data = NULL;
  databind_compiler_plugin_config config = {
      "image.generated.h", major, minor, patch};
  databind_compiler_projection_backend backend =
      databind_compiler_plugin_backend(&config);
  databind_compiler_projection_request request = {
      DATABIND_COMPILER_PROJECTION_PLUGIN, output_path, NULL};
  int status;

  if (tbe_compiler_parse_schema_file(
          schema_path, &root, &schema_data) != 0)
    return -1;

  status = databind_compiler_projection_run(
      root, &request, 1u, &backend, 1u);

  node_free(root);
  free(schema_data);
  return status;
}

spec("DataBind PLUGIN projection backend") {
describe("canonical native Service API") {
  it("publishes the same native symbol consumed by PLUGIN generation") {
    char *header;

    (void)remove(PLUGIN_NATIVE_HEADER_OUTPUT_FILE);
    (void)remove(PLUGIN_NATIVE_SOURCE_OUTPUT_FILE);

    check_equal(generate_native_header(), 0);

    header = tbe_compiler_read_file(PLUGIN_NATIVE_HEADER_OUTPUT_FILE);
    check_not_null(header);

    check_true(text_contains(
        header,
        "int databind_5_Image_5_Codec_6_Decode(\n"
        "    const DecodeRequest_t *request,\n"
        "    DecodeResponse_t *response);"));
    check_true(text_contains(
        header,
        "int databind_5_Image_5_Codec_6_Encode(\n"
        "    const EncodeRequest_t *request,\n"
        "    EncodeResponse_t *response);"));

    free(header);
    (void)remove(PLUGIN_NATIVE_HEADER_OUTPUT_FILE);
    (void)remove(PLUGIN_NATIVE_SOURCE_OUTPUT_FILE);
  }
}

describe("generated publication") {
  it("derives one passive Function export per Service operation") {
    char *generated;

    (void)remove(PLUGIN_PROJECTION_OUTPUT_FILE);
    check_equal(generate_plugin(
                    PLUGIN_PROJECTION_SCHEMA_FILE,
                    PLUGIN_PROJECTION_OUTPUT_FILE,
                    4u, 0u, 0u),
                0);
    check_true(file_exists(PLUGIN_PROJECTION_OUTPUT_FILE));

    generated = tbe_compiler_read_file(PLUGIN_PROJECTION_OUTPUT_FILE);
    check_not_null(generated);

    check_true(text_contains(
        generated, "#include <salts/plugin.h>"));
    check_true(text_contains(
        generated, "#include \"image.generated.h\""));

    check_true(text_contains(
        generated, ".plugin_id = \"Image\""));
    check_true(text_contains(
        generated, ".version = {4u, 0u, 0u}"));
    check_true(text_contains(
        generated, ".export_count = 2u"));

    check_true(text_contains(
        generated, ".export_id = \"Codec.Decode\""));
    check_true(text_contains(
        generated, ".export_id = \"Codec.Encode\""));
    check_true(text_contains(
        generated, ".contract_id = \"Codec\""));
    check_true(text_contains(
        generated, ".contract_version = 3u"));

    check_true(text_contains(
        generated, "CMETA_FUNCTION_METADATA_AS_ABI("));
    check_true(text_contains(
        generated, "CMETA_ABI_SCALAR"));
    check_true(text_contains(
        generated, "CMETA_ABI_POINTER"));
    check_true(text_contains(
        generated, "CMETA_PARAM_IN | CMETA_PARAM_BORROWED"));
    check_true(text_contains(
        generated, "CMETA_PARAM_OUT | CMETA_PARAM_BORROWED"));

    check_true(text_contains(
        generated, "extern int databind_5_Image_5_Codec_6_Decode("
                   "const DecodeRequest_t *request, DecodeResponse_t *response);"));
    check_true(text_contains(
        generated, "extern int databind_5_Image_5_Codec_6_Encode("
                   "const EncodeRequest_t *request, EncodeResponse_t *response);"));

    check_true(text_contains(
        generated, "static const salts_plugin_export plugin_exports[2]"));
    check_false(text_contains(generated, "plugin_export_ptrs"));
    check_false(text_contains(generated, "plugin_export_0"));
    check_false(text_contains(generated, "plugin_export_1"));

    check_false(text_contains(generated, "/decode"));
    check_false(text_contains(generated, "/encode"));
    check_false(text_contains(generated, "legacy.decode"));
    check_false(text_contains(generated, "http_method"));
    check_false(text_contains(generated, "rpc_name"));

    check_true(text_contains(
        generated,
        "return host_abi == SALTS_PLUGIN_ABI_VERSION ? &plugin_manifest : NULL;"));

    free(generated);
    (void)remove(PLUGIN_PROJECTION_OUTPUT_FILE);
  }

  it("uses the canonical DataBind native type identity") {
    char *generated;

    (void)remove(PLUGIN_PROJECTION_OUTPUT_FILE);
    check_equal(generate_plugin(
                    PLUGIN_PROJECTION_SCHEMA_FILE,
                    PLUGIN_PROJECTION_OUTPUT_FILE,
                    4u, 1u, 2u),
                0);

    generated = tbe_compiler_read_file(PLUGIN_PROJECTION_OUTPUT_FILE);
    check_not_null(generated);
    check_true(text_contains(
        generated,
        "CMETA_TYPE_ID_ATOM_INIT(\"tbe.native.Image.DecodeRequest_t\")"));
    check_true(text_contains(
        generated,
        "CMETA_TYPE_ID_ATOM_INIT(\"tbe.native.Image.DecodeResponse_t\")"));

    free(generated);
    (void)remove(PLUGIN_PROJECTION_OUTPUT_FILE);
  }
}

describe("projection admission") {
  it("requires an explicit version without touching an existing output") {
    char *preserved;
    static const char sentinel[] = "previous generated artifact\n";

    (void)remove(PLUGIN_PROJECTION_INVALID_OUTPUT_FILE);
    check_true(write_text(
        PLUGIN_PROJECTION_INVALID_OUTPUT_FILE, sentinel));

    check_equal(generate_plugin(
                    PLUGIN_PROJECTION_NO_VERSION_SCHEMA_FILE,
                    PLUGIN_PROJECTION_INVALID_OUTPUT_FILE,
                    4u, 0u, 0u),
                -1);
    check_true(file_exists(PLUGIN_PROJECTION_INVALID_OUTPUT_FILE));

    preserved = tbe_compiler_read_file(
        PLUGIN_PROJECTION_INVALID_OUTPUT_FILE);
    check_not_null(preserved);
    check_equal(strcmp(preserved, sentinel), 0);
    free(preserved);

    (void)remove(PLUGIN_PROJECTION_INVALID_OUTPUT_FILE);
  }

  it("rejects a PLUGIN request without an output path") {
    Node *root = NULL;
    char *schema_data = NULL;
    databind_compiler_plugin_config config = {
        "image.generated.h", 4u, 0u, 0u};
    databind_compiler_projection_backend backend =
        databind_compiler_plugin_backend(&config);
    databind_compiler_projection_request request = {
        DATABIND_COMPILER_PROJECTION_PLUGIN, NULL, NULL};

    check_equal(tbe_compiler_parse_schema_file(
                    PLUGIN_PROJECTION_SCHEMA_FILE,
                    &root, &schema_data),
                0);
    check_not_null(root);

    check_equal(databind_compiler_projection_run(
                    root, &request, 1u, &backend, 1u),
                -1);

    node_free(root);
    free(schema_data);
  }
}

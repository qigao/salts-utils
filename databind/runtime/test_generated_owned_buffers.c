#include "generated_owned_buffers.h"
#include "data_bind_native.h"
#include "data_bind_message_plan.h"
#include "json_cserde_reader.h"
#include "json_parser.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <cstl/byte_buffer.h>
#include <salts_cmeta_data.h>
#include <tstr.h>

#include <stdlib.h>
#include <string.h>

spec("generated owned buffers use canonical Salts CMeta lifecycle") {
  it("initializes moves and clears tstr and byte-buffer storage through one struct graph") {
    static const unsigned char payload[] = {0x41u, 0x00u, 0x42u, 0xffu};
    const cmeta_data_desc *native = NULL;
    const cmeta_data_struct_shape *shape = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    NativeOwnedBufferRecord_t source = {0};
    NativeOwnedBufferRecord_t destination = {0};
    size_t workspace_bytes = 0u;
    void *workspace = NULL;

    check_equal(NativeOwnedBufferRecord_cmeta_data(&native, &error), DATA_BIND_OK);
    check_not_null(native);
    if (native == NULL) return;

    check_true(cmeta_data_desc_valid(native));
    check_equal(native->kind, CMETA_DATA_STRUCT);
    check_true(cmeta_data_value_move_supported(native));
    shape = (const cmeta_data_struct_shape *)native->shape;
    check_not_null(shape);
    if (shape != NULL) {
      check_equal(shape->field_count, (size_t)3u);
      check_true(cmeta_data_desc_equal(shape->fields[1].value,
                                       &salts_tstr_cmeta_data));
      check_true(cmeta_data_desc_equal(shape->fields[2].value,
                                       &stl_byte_buffer_cmeta_data));
    }

    options.max_depth = 8u;
    options.max_items = 32u;
    options.max_owned_bytes = 4096u;
    check_equal(data_bind_native_probe_workspace_size(
                    options.max_depth, &workspace_bytes),
                DATA_BIND_OK);
    workspace = malloc(workspace_bytes);
    check_not_null(workspace);
    if (workspace == NULL) return;
    options.workspace = workspace;
    options.workspace_bytes = workspace_bytes;

    check_equal(data_bind_native_init(
                    &options, native, &source, sizeof(source), &diagnostic),
                DATA_BIND_OK);
    check_equal(data_bind_native_init(
                    &options, native, &destination, sizeof(destination),
                    &diagnostic),
                DATA_BIND_OK);

    check_null(source.text);
    check_equal(stl_byte_buffer_size(&source.payload), (size_t)0u);
    check_null(destination.text);
    check_equal(stl_byte_buffer_size(&destination.payload), (size_t)0u);

    source.id = 7u;
    source.text = tstr_dup("canonical-owned");
    check_not_null(source.text);
    check_equal(stl_byte_buffer_resize(&source.payload, sizeof(payload)), STL_OK);
    if (stl_byte_buffer_data(&source.payload) != NULL)
      memcpy(stl_byte_buffer_data(&source.payload), payload, sizeof(payload));

    check_equal(cmeta_data_value_move(native, &destination, &source), CMETA_OK);

    check_equal(source.id, 0u);
    check_null(source.text);
    check_equal(stl_byte_buffer_size(&source.payload), (size_t)0u);

    check_equal(destination.id, 7u);
    check_not_null(destination.text);
    if (destination.text != NULL) {
      check_equal(tstr_len(destination.text), strlen("canonical-owned"));
      check(memcmp(destination.text, "canonical-owned",
                   strlen("canonical-owned")) == 0);
    }
    check_equal(stl_byte_buffer_size(&destination.payload), sizeof(payload));
    if (stl_byte_buffer_data_const(&destination.payload) != NULL)
      check(memcmp(stl_byte_buffer_data_const(&destination.payload),
                   payload, sizeof(payload)) == 0);

    check_equal(data_bind_native_clear(
                    &options, native, &source, sizeof(source), &diagnostic),
                DATA_BIND_OK);
    check_equal(data_bind_native_clear(
                    &options, native, &destination, sizeof(destination),
                    &diagnostic),
                DATA_BIND_OK);
    check_equal(destination.id, 0u);
    check_null(destination.text);
    check_equal(stl_byte_buffer_size(&destination.payload), (size_t)0u);

    free(workspace);
  }
  it("decodes ordered duplicate-preserving header records through generated MessagePlan sequence metadata") {
    static const char valid_json[] =
        "{\"id\":7,\"headers\":["
        "{\"name\":\"x-tag\",\"value\":\"a\"},"
        "{\"name\":\"x-tag\",\"value\":\"b\"},"
        "{\"name\":\"y-tag\",\"value\":\"c\"}]}";
    static const char invalid_json[] =
        "{\"id\":8,\"headers\":["
        "{\"name\":\"x-tag\",\"value\":\"a\"},"
        "{\"name\":\"x-tag\"}]}";
    static const char invalid_nested_json[] =
        "{\"id\":10,\"headers\":["
        "{\"name\":\"x-tag\",\"value\":\"a\"},"
        "{\"name\":\"BAD!\",\"value\":\"b\"}]}";
    static const char oversized_json[] =
        "{\"id\":9,\"headers\":["
        "{\"name\":\"a\",\"value\":\"1\"},"
        "{\"name\":\"b\",\"value\":\"2\"},"
        "{\"name\":\"c\",\"value\":\"3\"},"
        "{\"name\":\"d\",\"value\":\"4\"}]}";
    const cmeta_data_desc *header_data = NULL;
    const cmeta_data_desc *policy_data = NULL;
    const cmeta_data_desc *sequence_data = NULL;
    const cmeta_data_desc *element_data = NULL;
    const cmeta_data_struct_shape *policy_shape = NULL;
    const DataBindMessageNativeArtifact *artifact =
        NativeHeaderPolicy_native_artifact();
    DataBindNativeTypeBinding binding =
        DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic plan_diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    NativeHeaderPolicy_t value = {0};
    NativeHeaderPolicy_t rejected = {0};
    NativeHeaderPolicy_t invalid_nested = {0};
    NativeHeaderPolicy_t oversized = {0};
    unsigned char workspace[16384] = {0};
    json_value_t *root = NULL;
    cserde_reader *reader = NULL;
    const NativeHeader_t *first;
    const NativeHeader_t *second;
    const NativeHeader_t *third;

    check_equal(NativeHeader_cmeta_data(&header_data, &error), DATA_BIND_OK);
    check_equal(NativeHeaderPolicy_cmeta_data(&policy_data, &error), DATA_BIND_OK);
    check_not_null(header_data);
    check_not_null(policy_data);
    if (header_data == NULL || policy_data == NULL) return;

    check_equal(
        cmeta_type_require_traits(
            header_data->storage_type,
            CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY),
        CMETA_OK);
    policy_shape = (const cmeta_data_struct_shape *)policy_data->shape;
    check_not_null(policy_shape);
    if (policy_shape == NULL) return;
    check_equal(policy_shape->field_count, (size_t)2u);
    sequence_data = policy_shape->fields[1].value;
    check_not_null(sequence_data);
    if (sequence_data == NULL) return;
    check_equal(sequence_data->kind, CMETA_DATA_SEQUENCE);
    check_not_null(cmeta_data_construct_ops_of(sequence_data));
    check_not_null(cmeta_data_collection_ops_of(sequence_data));
    element_data = cmeta_data_collection_element_data(sequence_data);
    check_not_null(element_data);
    if (element_data != NULL)
      check_true(cmeta_data_desc_equal(element_data, header_data));

    check_true(data_bind_message_native_artifact_valid(artifact));
    check_equal(artifact->native_binding(&binding, &error), DATA_BIND_OK);
    check_true(binding.data == policy_data);

    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "NativeHeaderPolicy", &binding, &plan, &plan_diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    options.max_depth = 16u;
    options.max_items = 64u;
    options.max_owned_bytes = 4096u;
    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);

    root = json_parse(valid_json, sizeof(valid_json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &plan_diagnostic),
          DATA_BIND_OK);
      check_equal(value.id, (uint32_t)7u);
      check_equal(NativeHeaderPolicy_headers_vec_t_size(&value.headers), (size_t)3u);
      first = NativeHeaderPolicy_headers_vec_t_at_const(&value.headers, 0u);
      second = NativeHeaderPolicy_headers_vec_t_at_const(&value.headers, 1u);
      third = NativeHeaderPolicy_headers_vec_t_at_const(&value.headers, 2u);
      check_not_null(first);
      check_not_null(second);
      check_not_null(third);
      if (first != NULL && second != NULL && third != NULL) {
        check_equal(tstr_len(first->name), strlen("x-tag"));
        check_equal(tstr_len(second->name), strlen("x-tag"));
        check_equal(tstr_len(third->name), strlen("y-tag"));
        check(memcmp(first->name, "x-tag", strlen("x-tag")) == 0);
        check(memcmp(second->name, "x-tag", strlen("x-tag")) == 0);
        check(memcmp(third->name, "y-tag", strlen("y-tag")) == 0);
        check(memcmp(first->value, "a", 1u) == 0);
        check(memcmp(second->value, "b", 1u) == 0);
        check(memcmp(third->value, "c", 1u) == 0);
      }
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
    reader = NULL;
    root = NULL;

    check_equal(
        data_bind_native_clear(
            &options, binding.data, &value, sizeof(value), &native_diagnostic),
        DATA_BIND_OK);
    check_equal(value.id, (uint32_t)0u);
    check_equal(NativeHeaderPolicy_headers_vec_t_size(&value.headers), (size_t)0u);

    plan_diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    root = json_parse(invalid_json, sizeof(invalid_json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_not_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &rejected, sizeof(rejected),
              &plan_diagnostic),
          DATA_BIND_OK);
      check_equal(rejected.id, (uint32_t)0u);
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&rejected.headers), (size_t)0u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
    reader = NULL;
    root = NULL;

    plan_diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    root = json_parse(
        invalid_nested_json, sizeof(invalid_nested_json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &invalid_nested,
              sizeof(invalid_nested), &plan_diagnostic),
          DATA_BIND_ERR_VALIDATION);
      check_contains(plan_diagnostic.schema_field, "headers[1].name");
      check_contains(plan_diagnostic.message, "Pattern");
      check_equal(invalid_nested.id, (uint32_t)0u);
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&invalid_nested.headers),
          (size_t)0u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
    reader = NULL;
    root = NULL;

    check_equal(
        data_bind_native_clear(
            &options, binding.data, &invalid_nested, sizeof(invalid_nested),
            &native_diagnostic),
        DATA_BIND_OK);

    plan_diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    root = json_parse(oversized_json, sizeof(oversized_json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &oversized, sizeof(oversized),
              &plan_diagnostic),
          DATA_BIND_ERR_VALIDATION);
      check_contains(plan_diagnostic.message, "Size");
      check_equal(oversized.id, (uint32_t)0u);
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&oversized.headers), (size_t)0u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);

    check_equal(
        data_bind_native_clear(
            &options, binding.data, &oversized, sizeof(oversized),
            &native_diagnostic),
        DATA_BIND_OK);

    check_equal(
        data_bind_native_clear(
            &options, binding.data, &rejected, sizeof(rejected),
            &native_diagnostic),
        DATA_BIND_OK);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

}

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

#include <stdio.h>
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
  it("decodes owning record maps through canonical typed CSTL Map metadata") {
    static const char json[] =
        "{\"id\":7,\"headers\":{"
        "\"alpha\":{\"name\":\"x-tag\",\"value\":\"a\"},"
        "\"beta\":{\"name\":\"y-tag\",\"value\":\"b\"}}}";
    const DataBindMessageNativeArtifact *artifact =
        NativeHeaderMap_native_artifact();
    DataBindNativeTypeBinding binding =
        DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *map_owner_data = NULL;
    const cmeta_data_struct_shape *shape = NULL;
    const cmeta_data_desc *map_data = NULL;
    const cmeta_data_desc *key_data = NULL;
    const cmeta_data_desc *value_data = NULL;
    DataBind *codec = NULL;
    NativeHeaderMap_t value = {0};
    unsigned char workspace[16384] = {0};
    json_value_t *root = NULL;
    cserde_reader *reader = NULL;
    tstr lookup = tstr_dup("alpha");
    const NativeHeader_t *stored = NULL;
    NativeHeaderMap_headers_map_t provider_probe = {0};
    NativeHeader_t provider_value = {0};
    tstr provider_key = tstr_dup("probe");
    cmeta_collector provider_collector = {0};
    cmeta_status provider_status;

    check_true(data_bind_message_native_artifact_valid(artifact));
    check_equal(artifact->native_binding(&binding, &error), DATA_BIND_OK);
    check_equal(NativeHeaderMap_cmeta_data(&map_owner_data, &error), DATA_BIND_OK);
    check_not_null(binding.data);
    check_not_null(map_owner_data);
    if (binding.data == NULL || map_owner_data == NULL) {
      tstr_free(lookup);
      return;
    }
    check_true(binding.data == map_owner_data);

    shape = (const cmeta_data_struct_shape *)map_owner_data->shape;
    check_not_null(shape);
    if (shape == NULL || shape->field_count != 2u) {
      tstr_free(lookup);
      return;
    }
    map_data = shape->fields[1].value;
    check_not_null(map_data);
    if (map_data == NULL) {
      tstr_free(lookup);
      return;
    }
    check_equal(map_data->kind, CMETA_DATA_MAP);
    check_true(cmeta_data_desc_equal(
        map_data, &NativeHeaderMap_headers_map_t_map_data));
    key_data = cmeta_data_map_key_data(map_data);
    value_data = cmeta_data_map_value_data(map_data);
    check_true(cmeta_data_desc_equal(key_data, SALTS_TSTR_CMETA_DATA_REF));
    check_true(value_data == &NativeHeader_CMETA_DATA);
    check_true(value_data->storage_type == &NativeHeader_CMETA_TYPE);
    check_not_null(cmeta_data_construct_ops_of(map_data));

    /* Isolate canonical typed Map provider admission from MessagePlan/CSerde. */
    check_not_null(provider_key);
    check_equal(
        cmeta_data_value_init_zero(map_data, &provider_probe), CMETA_OK);
    check_equal(
        cmeta_data_value_init_zero(value_data, &provider_value), CMETA_OK);
    provider_value.name = tstr_dup("x-tag");
    provider_value.value = tstr_dup("probe-value");
    check_not_null(provider_value.name);
    check_not_null(provider_value.value);
    check_true(cmeta_type_equal(
        provider_probe.raw.key_type, SALTS_TSTR_CMETA_TYPE_REF));
    check_true(cmeta_type_equal(
        provider_probe.raw.value_type, &NativeHeader_CMETA_TYPE));
    check_equal(
        cmeta_data_map_collector(
            map_data, &provider_probe, 4u, &provider_collector),
        CMETA_OK);
    check_equal(cmeta_collector_begin(&provider_collector), CMETA_OK);
    provider_status = cmeta_data_map_accept(
        map_data, &provider_collector,
        key_data, &provider_key, value_data, &provider_value);
    check_equal(provider_status, CMETA_OK);
    if (provider_status == CMETA_OK)
      check_equal(cmeta_collector_finish(&provider_collector), CMETA_OK);
    else
      cmeta_collector_abort(&provider_collector);
    check_equal(
        NativeHeaderMap_headers_map_t_size(&provider_probe), (size_t)1u);
    check_equal(
        cmeta_data_value_restore_zero(value_data, &provider_value), CMETA_OK);
    check_equal(
        cmeta_data_value_restore_zero(map_data, &provider_probe), CMETA_OK);
    tstr_free(provider_key);
    provider_key = NULL;

    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) {
      tstr_free(lookup);
      return;
    }
    check_equal(
        data_bind_message_plan_compile(
            codec, "NativeHeaderMap", &binding, &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      tstr_free(lookup);
      return;
    }

    options.max_depth = 16u;
    options.max_items = 64u;
    options.max_owned_bytes = 4096u;
    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);

    root = json_parse(json, sizeof(json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      DataBindStatus decode_status =
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic);
      if (decode_status != DATA_BIND_OK)
        fprintf(
            stderr,
            "record-map decode status=%d field=%s message=%s\n",
            (int)decode_status,
            diagnostic.schema_field[0] != '\0'
                ? diagnostic.schema_field : "<root>",
            diagnostic.message[0] != '\0'
                ? diagnostic.message : "<none>");
      check_equal(decode_status, DATA_BIND_OK);
      check_equal(value.id, (uint32_t)7u);
      check_equal(
          NativeHeaderMap_headers_map_t_size(&value.headers), (size_t)2u);
      check_not_null(lookup);
      if (lookup != NULL) {
        stored =
            NativeHeaderMap_headers_map_t_get_const(&value.headers, lookup);
        check_not_null(stored);
        if (stored != NULL) {
          check_not_null(stored->name);
          check_not_null(stored->value);
          if (stored->name != NULL) {
            check_equal(tstr_len(stored->name), strlen("x-tag"));
            check(memcmp(stored->name, "x-tag", strlen("x-tag")) == 0);
          }
          if (stored->value != NULL) {
            check_equal(tstr_len(stored->value), (size_t)1u);
            check(memcmp(stored->value, "a", 1u) == 0);
          }
        }
      }
    }
    json_cserde_reader_destroy(reader);
    json_free(root);

    check_equal(
        data_bind_native_clear(
            &options, binding.data, &value, sizeof(value), &native_diagnostic),
        DATA_BIND_OK);
    check_equal(value.id, (uint32_t)0u);
    check_equal(
        NativeHeaderMap_headers_map_t_size(&value.headers), (size_t)0u);
    check_null(value.headers.cmeta.descriptor);
    check_null(value.headers.raw.impl);

    tstr_free(lookup);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("decodes ordered duplicate-preserving header records through generated MessagePlan sequence metadata") {
    static const char empty_json[] =
        "{\"id\":6,\"headers\":[]}";
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
    NativeHeaderPolicy_t empty = {0};
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
    NativeHeaderPolicy_headers_vec_t manual_headers = {0};
    NativeHeader_t manual_header = {0};
    const NativeHeader_t *manual_copy = NULL;

    check_equal(NativeHeader_cmeta_data(&header_data, &error), DATA_BIND_OK);
    check_equal(NativeHeaderPolicy_cmeta_data(&policy_data, &error), DATA_BIND_OK);
    check_not_null(header_data);
    check_not_null(policy_data);
    if (header_data == NULL || policy_data == NULL) return;
    check_true(header_data == &NativeHeader_CMETA_DATA);
    check_true(header_data->storage_type == &NativeHeader_CMETA_TYPE);

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
    check_true(cmeta_data_desc_equal(
        sequence_data, &NativeHeaderPolicy_headers_vec_t_collection_data));
    check_not_null(cmeta_data_construct_ops_of(sequence_data));
    check_not_null(cmeta_data_collection_ops_of(sequence_data));
    element_data = cmeta_data_collection_element_data(sequence_data);
    check_not_null(element_data);
    if (element_data != NULL) {
      check_true(element_data == &NativeHeader_CMETA_DATA);
      check_true(cmeta_data_desc_equal(element_data, header_data));
    }

    check_equal(
        NativeHeaderPolicy_headers_vec_t_init(&manual_headers, 4u), STL_OK);
    check_not_null(manual_headers.cmeta.descriptor);
    check_not_null(manual_headers.raw.element_type);
    if (manual_headers.raw.element_type != NULL)
      check_true(cmeta_type_equal(
          manual_headers.raw.element_type, header_data->storage_type));
    manual_header.name = tstr_dup("x-tag");
    manual_header.value = tstr_dup("manual");
    check_not_null(manual_header.name);
    check_not_null(manual_header.value);
    if (manual_header.name != NULL && manual_header.value != NULL)
      check_equal(
          NativeHeaderPolicy_headers_vec_t_push(
              &manual_headers, manual_header),
          STL_OK);
    check_equal(
        cmeta_data_value_restore_zero(header_data, &manual_header), CMETA_OK);
    manual_copy =
        NativeHeaderPolicy_headers_vec_t_at_const(&manual_headers, 0u);
    check_not_null(manual_copy);
    if (manual_copy != NULL) {
      check_equal(tstr_len(manual_copy->name), strlen("x-tag"));
      check_equal(tstr_len(manual_copy->value), strlen("manual"));
      check(memcmp(manual_copy->name, "x-tag", strlen("x-tag")) == 0);
      check(memcmp(manual_copy->value, "manual", strlen("manual")) == 0);
    }
    NativeHeaderPolicy_headers_vec_t_destroy(&manual_headers);
    check_equal(
        NativeHeaderPolicy_headers_vec_t_size(&manual_headers), (size_t)0u);

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

    root = json_parse(empty_json, sizeof(empty_json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &empty, sizeof(empty), &plan_diagnostic),
          DATA_BIND_OK);
      check_equal(empty.id, (uint32_t)6u);
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&empty.headers), (size_t)0u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
    reader = NULL;
    root = NULL;
    check_equal(
        data_bind_native_clear(
            &options, binding.data, &empty, sizeof(empty), &native_diagnostic),
        DATA_BIND_OK);

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

  it("runs nested element validation even without a list-level rule") {
    static const char invalid_json[] =
        "{\"id\":11,\"headers\":[{\"name\":\"BAD!\",\"value\":\"x\"}]}";
    const DataBindMessageNativeArtifact *artifact =
        NativeHeaderNestedOnly_native_artifact();
    DataBindNativeTypeBinding binding =
        DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    NativeHeaderNestedOnly_t value = {0};
    unsigned char workspace[8192] = {0};
    json_value_t *root = NULL;
    cserde_reader *reader = NULL;

    check_true(data_bind_message_native_artifact_valid(artifact));
    check_equal(artifact->native_binding(&binding, &error), DATA_BIND_OK);
    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    check_equal(
        data_bind_message_plan_compile(
            codec, "NativeHeaderNestedOnly", &binding, &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    options.max_depth = 16u;
    options.max_items = 32u;
    options.max_owned_bytes = 2048u;
    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);

    root = json_parse(invalid_json, sizeof(invalid_json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic),
          DATA_BIND_ERR_VALIDATION);
      check_contains(diagnostic.schema_field, "headers[0].name");
      check_contains(diagnostic.message, "Pattern");
      check_equal(value.id, (uint32_t)0u);
      check_equal(
          NativeHeaderNestedOnly_headers_vec_t_size(&value.headers),
          (size_t)0u);
    }

    json_cserde_reader_destroy(reader);
    json_free(root);
    check_equal(
        data_bind_native_clear(
            &options, binding.data, &value, sizeof(value),
            &native_diagnostic),
        DATA_BIND_OK);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("rolls back generated sequences on item and depth quota failures") {
    const DataBindMessageNativeArtifact *artifact =
        NativeHeaderPolicy_native_artifact();
    DataBindNativeTypeBinding binding =
        DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
    const cmeta_data_struct_shape *shape;
    const cmeta_data_desc *sequence_data;
    DataBindNativeRequirements requirements =
        DATA_BIND_NATIVE_REQUIREMENTS_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindMessagePlan *plan = NULL;
    DataBind *codec = NULL;
    NativeHeaderPolicy_t value = {0};
    unsigned char workspace[16384] = {0};
    char json[4096];
    size_t used = 0u;
    size_t count;
    size_t i;
    json_value_t *root = NULL;
    cserde_reader *reader = NULL;

    check_equal(artifact->native_binding(&binding, &error), DATA_BIND_OK);
    check_not_null(binding.data);
    if (binding.data == NULL || binding.data->shape == NULL) return;
    shape = (const cmeta_data_struct_shape *)binding.data->shape;
    check_equal(shape->field_count, (size_t)2u);
    sequence_data = shape->fields[1].value;
    check_not_null(sequence_data);
    if (sequence_data == NULL) return;

    options.max_depth = 16u;
    options.max_items = 64u;
    options.max_owned_bytes = 4096u;
    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);
    check_equal(
        data_bind_native_measure(
            &options, sequence_data, &requirements, &native_diagnostic),
        DATA_BIND_OK);
    check_true(requirements.descriptor_nodes > 0u);
    check_true(requirements.descriptor_nodes < 32u);
    check_true(requirements.descriptor_depth > 1u);
    if (requirements.descriptor_nodes == 0u ||
        requirements.descriptor_nodes >= 32u ||
        requirements.descriptor_depth <= 1u)
      return;

    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "NativeHeaderPolicy", &binding, &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    count = requirements.descriptor_nodes + 1u;
    used = (size_t)snprintf(json, sizeof(json), "{\"id\":21,\"headers\":[");
    for (i = 0u; i < count && used < sizeof(json); ++i) {
      int written = snprintf(
          json + used, sizeof(json) - used,
          "%s{\"name\":\"x\",\"value\":\"y\"}", i == 0u ? "" : ",");
      check_true(written > 0);
      if (written <= 0 || (size_t)written >= sizeof(json) - used) {
        used = sizeof(json);
        break;
      }
      used += (size_t)written;
    }
    if (used < sizeof(json)) {
      int written = snprintf(json + used, sizeof(json) - used, "]}");
      check_true(written > 0);
      if (written > 0 && (size_t)written < sizeof(json) - used)
        used += (size_t)written;
      else
        used = sizeof(json);
    }
    check_true(used < sizeof(json));
    if (used >= sizeof(json)) {
      data_bind_message_plan_free(plan);
      data_bind_free(codec);
      return;
    }

    options.max_items = requirements.descriptor_nodes;
    root = json_parse(json, used);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic),
          DATA_BIND_ERR_LIMIT);
      check_equal(value.id, (uint32_t)0u);
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&value.headers), (size_t)0u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
    reader = NULL;
    root = NULL;

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    options.max_items = 64u;
    options.max_depth = requirements.descriptor_depth - 1u;
    root = json_parse(
        "{\"id\":22,\"headers\":[{\"name\":\"x\",\"value\":\"y\"}]}",
        sizeof("{\"id\":22,\"headers\":[{\"name\":\"x\",\"value\":\"y\"}]}") - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic),
          DATA_BIND_ERR_LIMIT);
      check_equal(value.id, (uint32_t)0u);
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&value.headers), (size_t)0u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
    reader = NULL;
    root = NULL;

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    options.max_depth = 16u;
    options.max_owned_bytes = 1u;
    root = json_parse(
        "{\"id\":23,\"headers\":[{\"name\":\"x\",\"value\":\"y\"}]}",
        sizeof("{\"id\":23,\"headers\":[{\"name\":\"x\",\"value\":\"y\"}]}") - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic),
          DATA_BIND_ERR_LIMIT);
      check_equal(value.id, (uint32_t)0u);
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&value.headers), (size_t)0u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);

    options.max_owned_bytes = 4096u;
    check_equal(
        data_bind_native_clear(
            &options, binding.data, &value, sizeof(value),
            &native_diagnostic),
        DATA_BIND_OK);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }


  it("enforces max_owned_bytes across separate MessagePlan fields") {
    static const char json[] =
        "{\"first\":\"ab\",\"second\":\"cd\"}";
    const DataBindMessageNativeArtifact *artifact =
        NativeAggregateOwnedBudget_native_artifact();
    DataBindNativeTypeBinding binding =
        DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    NativeAggregateOwnedBudget_t value = {0};
    unsigned char workspace[8192] = {0};
    json_value_t *root = NULL;
    cserde_reader *reader = NULL;

    check_true(data_bind_message_native_artifact_valid(artifact));
    check_equal(artifact->native_binding(&binding, &error), DATA_BIND_OK);
    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "NativeAggregateOwnedBudget", &binding,
            &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    options.max_depth = 16u;
    options.max_items = 64u;
    options.max_owned_bytes = 3u;
    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);

    root = json_parse(json, sizeof(json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic),
          DATA_BIND_ERR_LIMIT);
      check_null(value.first);
      check_null(value.second);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
    reader = NULL;
    root = NULL;

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    options.max_owned_bytes = 4u;
    root = json_parse(json, sizeof(json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic),
          DATA_BIND_OK);
      check_not_null(value.first);
      check_not_null(value.second);
      if (value.first != NULL) {
        check_equal(tstr_len(value.first), (size_t)2u);
        check(memcmp(value.first, "ab", 2u) == 0);
      }
      if (value.second != NULL) {
        check_equal(tstr_len(value.second), (size_t)2u);
        check(memcmp(value.second, "cd", 2u) == 0);
      }
    }
    json_cserde_reader_destroy(reader);
    json_free(root);

    check_equal(
        data_bind_native_clear(
            &options, binding.data, &value, sizeof(value),
            &native_diagnostic),
        DATA_BIND_OK);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("enforces max_items across separate MessagePlan sequence fields") {
    static const char json[] =
        "{\"left\":["
        "{\"name\":\"a\",\"value\":\"x\"},"
        "{\"name\":\"b\",\"value\":\"y\"}],"
        "\"right\":["
        "{\"name\":\"c\",\"value\":\"u\"},"
        "{\"name\":\"d\",\"value\":\"v\"}]}";
    const DataBindMessageNativeArtifact *artifact =
        NativeAggregateItemBudget_native_artifact();
    DataBindNativeTypeBinding binding =
        DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
    DataBindNativeRequirements requirements =
        DATA_BIND_NATIVE_REQUIREMENTS_INIT;
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindNativeDiagnostic native_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    NativeAggregateItemBudget_t value = {0};
    unsigned char workspace[16384] = {0};
    json_value_t *root = NULL;
    cserde_reader *reader = NULL;

    check_true(data_bind_message_native_artifact_valid(artifact));
    check_equal(artifact->native_binding(&binding, &error), DATA_BIND_OK);
    check_not_null(binding.data);
    if (binding.data == NULL) return;

    options.max_depth = 16u;
    options.max_items = 64u;
    options.max_owned_bytes = 4096u;
    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);
    check_equal(
        data_bind_native_measure(
            &options, binding.data, &requirements, &native_diagnostic),
        DATA_BIND_OK);
    check_true(requirements.descriptor_nodes > 1u);
    check_true(requirements.descriptor_nodes < 64u);
    if (requirements.descriptor_nodes <= 1u ||
        requirements.descriptor_nodes >= 64u)
      return;

    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_message_plan_compile(
            codec, "NativeAggregateItemBudget", &binding,
            &plan, &diagnostic),
        DATA_BIND_OK);
    check_not_null(plan);
    if (plan == NULL) {
      data_bind_free(codec);
      return;
    }

    options.max_items = requirements.descriptor_nodes;
    root = json_parse(json, sizeof(json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic),
          DATA_BIND_ERR_LIMIT);
      check_equal(
          NativeAggregateItemBudget_left_vec_t_size(&value.left), (size_t)0u);
      check_equal(
          NativeAggregateItemBudget_right_vec_t_size(&value.right), (size_t)0u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
    reader = NULL;
    root = NULL;

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    options.max_items = 64u;
    root = json_parse(json, sizeof(json) - 1u);
    check_not_null(root);
    if (root != NULL) reader = json_cserde_reader_create(root, 16u);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(
          data_bind_message_plan_decode_native(
              plan, &options, reader, &value, sizeof(value), &diagnostic),
          DATA_BIND_OK);
      check_equal(
          NativeAggregateItemBudget_left_vec_t_size(&value.left), (size_t)2u);
      check_equal(
          NativeAggregateItemBudget_right_vec_t_size(&value.right), (size_t)2u);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);

    check_equal(
        data_bind_native_clear(
            &options, binding.data, &value, sizeof(value),
            &native_diagnostic),
        DATA_BIND_OK);
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

}

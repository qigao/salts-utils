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
  it("routes generated Message JSON YAML input and JSON YAML output canonically") {
    static const char json[] =
        "{\"id\":7,\"headers\":["
        "{\"name\":\"x-tag\",\"value\":\"a\"},"
        "{\"name\":\"y-tag\",\"value\":\"b\"}]}";
    static const char yaml[] =
        "id: 8\n"
        "headers:\n"
        "  - name: x-tag\n"
        "    value: alpha\n";
    static const char invalid[] =
        "{\"id\":9,\"headers\":[{\"name\":\"x-tag\"}]}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    NativeHeaderPolicy_t from_json = {0};
    NativeHeaderPolicy_t from_yaml = {0};
    NativeHeaderPolicy_t roundtrip = {0};
    NativeHeaderPolicy_t yaml_roundtrip = {0};
    NativeHeaderPolicy_t unchanged = {0};
    const NativeHeader_t *header = NULL;
    char *encoded = NULL;
    char *encoded_yaml = NULL;
    uint8_t *legacy_binary = NULL;
    size_t encoded_len = 0u;
    size_t encoded_yaml_len = 0u;
    size_t legacy_binary_len = 0u;

    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    NativeHeaderPolicy_init(&from_json);
    NativeHeaderPolicy_init(&from_yaml);
    NativeHeaderPolicy_init(&roundtrip);
    NativeHeaderPolicy_init(&yaml_roundtrip);
    NativeHeaderPolicy_init(&unchanged);

    check_equal(
        NativeHeaderPolicy_from_json(
            codec, &from_json, json, sizeof(json) - 1u, &error),
        DATA_BIND_OK);
    check_equal(from_json.id, UINT32_C(7));
    check_not_null(from_json.headers.cmeta.descriptor);
    check_not_null(from_json.headers.raw.element_type);
    check_true(cmeta_type_equal(
        from_json.headers.raw.element_type, &NativeHeader_CMETA_TYPE));
    check_equal(
        NativeHeaderPolicy_headers_vec_t_size(&from_json.headers),
        (size_t)2u);
    header = NativeHeaderPolicy_headers_vec_t_at_const(
        &from_json.headers, 0u);
    check_not_null(header);
    if (header != NULL) {
      check_not_null(header->name);
      check_not_null(header->value);
      if (header->name != NULL) {
        check_equal(tstr_len(header->name), strlen("x-tag"));
        check(memcmp(header->name, "x-tag", strlen("x-tag")) == 0);
      }
      if (header->value != NULL) {
        check_equal(tstr_len(header->value), (size_t)1u);
        check(memcmp(header->value, "a", 1u) == 0);
      }
    }

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeHeaderPolicy_from_yaml(
            codec, &from_yaml, yaml, sizeof(yaml) - 1u, &error),
        DATA_BIND_OK);
    check_equal(from_yaml.id, UINT32_C(8));
    check_equal(
        NativeHeaderPolicy_headers_vec_t_size(&from_yaml.headers),
        (size_t)1u);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeHeaderPolicy_to_json(
            codec, &from_json, &encoded, &encoded_len, &error),
        DATA_BIND_OK);
    check_not_null(encoded);
    check_true(encoded_len != 0u);
    if (encoded != NULL) {
      check(encoded[encoded_len] == '\0');
      check_not_null(strstr(encoded, "\"policyId\":7"));
      check_null(strstr(encoded, "\"id\":"));
      check_not_null(strstr(encoded, "\"headers\":["));
      check_equal(
          NativeHeaderPolicy_from_json(
              codec, &roundtrip, encoded, encoded_len, &error),
          DATA_BIND_OK);
      check_equal(roundtrip.id, UINT32_C(7));
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&roundtrip.headers),
          (size_t)2u);
    }

    unchanged.id = UINT32_C(77);
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check(
        NativeHeaderPolicy_from_json(
            codec, &unchanged, invalid, sizeof(invalid) - 1u, &error) !=
        DATA_BIND_OK);
    check_equal(unchanged.id, UINT32_C(77));
    check_equal(
        NativeHeaderPolicy_headers_vec_t_size(&unchanged.headers),
        (size_t)0u);

    /* Binary remains outside this text-egress slice. */
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeHeaderPolicy_to_bin(
            codec, &from_json, &legacy_binary, &legacy_binary_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(legacy_binary);
    check_equal(legacy_binary_len, (size_t)0u);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeHeaderPolicy_to_yaml(
            codec, &from_json, &encoded_yaml, &encoded_yaml_len, &error),
        DATA_BIND_OK);
    check_not_null(encoded_yaml);
    check_true(encoded_yaml_len != 0u);
    if (encoded_yaml != NULL) {
      check(encoded_yaml[encoded_yaml_len] == '\0');
      check_not_null(strstr(encoded_yaml, "policyId"));
      check_null(strstr(encoded_yaml, "id:"));
      check_equal(
          NativeHeaderPolicy_from_yaml(
              codec, &yaml_roundtrip, encoded_yaml, encoded_yaml_len, &error),
          DATA_BIND_OK);
      check_equal(yaml_roundtrip.id, UINT32_C(7));
      check_equal(
          NativeHeaderPolicy_headers_vec_t_size(&yaml_roundtrip.headers),
          (size_t)2u);
    }

    data_bind_serialized_free(encoded_yaml);
    data_bind_serialized_free(encoded);
    NativeHeaderPolicy_clear(&unchanged);
    NativeHeaderPolicy_clear(&yaml_roundtrip);
    NativeHeaderPolicy_clear(&roundtrip);
    NativeHeaderPolicy_clear(&from_yaml);
    NativeHeaderPolicy_clear(&from_json);
    data_bind_free(codec);
  }

  it("routes only flat generated Message XML through canonical provider") {
    static const char xml[] =
        "<NativeXmlFlat>"
        "<id>9</id><name>alice</name><score>41</score>"
        "</NativeXmlFlat>";
    static const char invalid_xml[] =
        "<NativeXmlFlat><id>bad</id><name>alice</name><score>41</score>"
        "</NativeXmlFlat>";
    static const char collection_xml[] =
        "<NativeHeaderPolicy><id>7</id>"
        "<headers><name>x-tag</name><value>a</value></headers>"
        "</NativeHeaderPolicy>";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    NativeXmlFlat_t flat = {0};
    NativeXmlFlat_t unchanged = {0};
    NativeHeaderPolicy_t collection = {0};

    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    NativeXmlFlat_init(&flat);
    NativeXmlFlat_init(&unchanged);
    NativeHeaderPolicy_init(&collection);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    {
      DataBindStatus xml_status =
          NativeXmlFlat_from_xml(
              codec, &flat, xml, sizeof(xml) - 1u, &error);
      info("flat XML status=%d path=%s message=%s",
           (int)xml_status,
           error.path[0] != '\0' ? error.path : "<root>",
           error.message[0] != '\0' ? error.message : "<none>");
      check_equal(xml_status, DATA_BIND_OK);
    }
    check_equal(flat.id, UINT32_C(9));
    check_not_null(flat.name);
    if (flat.name != NULL) {
      check_equal(tstr_len(flat.name), (size_t)5u);
      check(memcmp(flat.name, "alice", 5u) == 0);
    }
    check_equal(flat.score, UINT32_C(41));

    unchanged.id = UINT32_C(77);
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeXmlFlat_from_xml(
            codec, &unchanged, invalid_xml, sizeof(invalid_xml) - 1u,
            &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(unchanged.id, UINT32_C(77));
    check_null(unchanged.name);
    check_equal(unchanged.score, UINT32_C(0));

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeHeaderPolicy_from_xml(
            codec, &collection, collection_xml, sizeof(collection_xml) - 1u,
            &error),
        DATA_BIND_ERR_SCHEMA);
    check_equal(collection.id, UINT32_C(0));
    check_equal(
        NativeHeaderPolicy_headers_vec_t_size(&collection.headers),
        (size_t)0u);

    NativeHeaderPolicy_clear(&collection);
    NativeXmlFlat_clear(&unchanged);
    NativeXmlFlat_clear(&flat);
    data_bind_free(codec);
  }

  it("routes flat generated Message XML output through canonical writer") {
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    NativeXmlOutputFlat_t source = {0};
    NativeXmlOutputFlat_t roundtrip = {0};
    NativeXmlFlat_t nullable = {0};
    char *xml = NULL;
    size_t xml_len = 0u;

    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    NativeXmlOutputFlat_init(&source);
    NativeXmlOutputFlat_init(&roundtrip);
    NativeXmlFlat_init(&nullable);

    source.id = UINT32_C(11);
    source.name = tstr_dup("a&b");
    check_not_null(source.name);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    {
      DataBindStatus xml_status =
          NativeXmlOutputFlat_to_xml(
              codec, &source, &xml, &xml_len, &error);
      info("flat XML output status=%d path=%s message=%s",
           (int)xml_status,
           error.path[0] != '\0' ? error.path : "<root>",
           error.message[0] != '\0' ? error.message : "<none>");
      check_equal(xml_status, DATA_BIND_OK);
    }
    check_not_null(xml);
    check_true(xml_len != 0u);
    if (xml != NULL) {
      check(xml[xml_len] == '\0');
      check_not_null(strstr(xml, "<NativeXmlOutputFlat>"));
      check_not_null(strstr(xml, "<wireId>11</wireId>"));
      check_null(strstr(xml, "<id>11</id>"));
      check_not_null(strstr(xml, "<name>a&amp;b</name>"));

      info("flat XML output bytes=%s", xml);
      error = (DataBindError)DATA_BIND_ERROR_INIT;
      {
        DataBindStatus roundtrip_status =
            NativeXmlOutputFlat_from_xml(
                codec, &roundtrip, xml, xml_len, &error);
        info("flat XML roundtrip status=%d path=%s message=%s",
             (int)roundtrip_status,
             error.path[0] != '\0' ? error.path : "<root>",
             error.message[0] != '\0' ? error.message : "<none>");
        check_equal(roundtrip_status, DATA_BIND_OK);
      }
      check_equal(roundtrip.id, UINT32_C(11));
      check_not_null(roundtrip.name);
      if (roundtrip.name != NULL) {
        check_equal(tstr_len(roundtrip.name), (size_t)3u);
        check(memcmp(roundtrip.name, "a&b", 3u) == 0);
      }
    }

    /* XML cannot preserve explicit NULL, so nullable contracts stay historical
     * and fail closed instead of silently collapsing NULL state. */
    nullable.id = UINT32_C(9);
    nullable.name = tstr_dup("nullable");
    check_not_null(nullable.name);
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    {
      char *rejected = NULL;
      size_t rejected_len = 0u;
      check_equal(
          NativeXmlFlat_to_xml(
              codec, &nullable, &rejected, &rejected_len, &error),
          DATA_BIND_ERR_SCHEMA);
      check_null(rejected);
      check_equal(rejected_len, (size_t)0u);
    }

    data_bind_serialized_free(xml);
    NativeXmlFlat_clear(&nullable);
    NativeXmlOutputFlat_clear(&roundtrip);
    NativeXmlOutputFlat_clear(&source);
    data_bind_free(codec);
  }

  it("routes exact flat generated CSV rows through canonical provider") {
    static const char csv[] =
        "id,name\n"
        "7,alice\n"
        "8,bob\n";
    static const char invalid_csv[] =
        "id,name\n"
        "bad,alice\n";
    static const char empty_required_csv[] =
        "id,name\n"
        "8,\n";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    NativeCsvFlat_t value = {0};
    NativeCsvFlat_t unchanged = {0};
    tstr preserved_name = NULL;

    check_equal(NativeOwnedBuffers_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    NativeCsvFlat_init(&value);
    NativeCsvFlat_init(&unchanged);

    check_equal(
        NativeCsvFlat_from_csv(
            codec, &value, csv, sizeof(csv) - 1u, 1u, &error),
        DATA_BIND_OK);
    check_equal(value.id, UINT32_C(8));
    check_not_null(value.name);
    if (value.name != NULL) {
      check_equal(tstr_len(value.name), (size_t)3u);
      check(memcmp(value.name, "bob", 3u) == 0);
    }

    unchanged.id = UINT32_C(77);
    unchanged.name = tstr_dup("keep");
    check_not_null(unchanged.name);
    preserved_name = unchanged.name;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeCsvFlat_from_csv(
            codec, &unchanged, invalid_csv, sizeof(invalid_csv) - 1u,
            0u, &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(unchanged.id, UINT32_C(77));
    check_true(unchanged.name == preserved_name);
    if (unchanged.name != NULL) {
      check_equal(tstr_len(unchanged.name), (size_t)4u);
      check(memcmp(unchanged.name, "keep", 4u) == 0);
    }

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeCsvFlat_from_csv(
            codec, &unchanged, csv, sizeof(csv) - 1u, 2u, &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(unchanged.id, UINT32_C(77));
    check_true(unchanged.name == preserved_name);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        NativeCsvFlat_from_csv(
            codec, &unchanged,
            empty_required_csv, sizeof(empty_required_csv) - 1u,
            0u, &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(unchanged.id, UINT32_C(77));
    check_true(unchanged.name == preserved_name);

    NativeCsvFlat_clear(&unchanged);
    NativeCsvFlat_clear(&value);
    data_bind_free(codec);
  }

  it("decodes owning record maps through canonical typed CSTL Map metadata") {
    static const char json[] =
        "{\"id\":7,\"headers\":{"
        "\"alpha\":{\"name\":\"x-tag\",\"value\":\"a\"},"
        "\"beta\":{\"name\":\"y-tag\",\"value\":\"b\"}}}";
    static const char map_json[] =
        "{\"alpha\":{\"name\":\"x-tag\",\"value\":\"a\"},"
        "\"beta\":{\"name\":\"y-tag\",\"value\":\"b\"}}";
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
    NativeHeaderMap_t validation_owner = {0};
    NativeHeaderMap_headers_map_t direct_map = {0};
    DataBindNativeDiagnostic direct_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindError validation_error = DATA_BIND_ERROR_INIT;
    unsigned char workspace[16384] = {0};
    json_value_t *root = NULL;
    json_value_t *map_root = NULL;
    cserde_reader *reader = NULL;
    cserde_reader *map_reader = NULL;
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

    /* Isolate native Map decode from MessagePlan's prefixed field reader. */
    map_root = json_parse(map_json, sizeof(map_json) - 1u);
    check_not_null(map_root);
    if (map_root != NULL)
      map_reader = json_cserde_reader_create(map_root, 16u);
    check_not_null(map_reader);
    check_equal(
        cmeta_data_value_init_zero(map_data, &direct_map), CMETA_OK);
    if (map_reader != NULL) {
      DataBindStatus direct_status =
          data_bind_native_decode(
              &options, map_data, map_reader,
              &direct_map, sizeof(direct_map), &direct_diagnostic);
      if (direct_status != DATA_BIND_OK)
        fprintf(
            stderr,
            "record-map direct-native status=%d path=%s message=%s\n",
            (int)direct_status,
            direct_diagnostic.error.path[0] != '\0'
                ? direct_diagnostic.error.path : "<root>",
            direct_diagnostic.error.message[0] != '\0'
                ? direct_diagnostic.error.message : "<none>");
      check_equal(direct_status, DATA_BIND_OK);
      if (direct_status == DATA_BIND_OK) {
        DataBindStatus validation_status;
        check_equal(
            NativeHeaderMap_headers_map_t_size(&direct_map), (size_t)2u);

        NativeHeaderMap_init(&validation_owner);
        validation_owner.id = UINT32_C(7);
        check_equal(
            cmeta_data_value_move(
                map_data, &validation_owner.headers, &direct_map),
            CMETA_OK);
        validation_status =
            data_bind_message_plan_validate_native(
                plan, &validation_owner, sizeof(validation_owner),
                &validation_error);
        if (validation_status != DATA_BIND_OK)
          fprintf(
              stderr,
              "record-map validation status=%d path=%s message=%s\n",
              (int)validation_status,
              validation_error.path[0] != '\0'
                  ? validation_error.path : "<root>",
              validation_error.message[0] != '\0'
                  ? validation_error.message : "<none>");
        check_equal(validation_status, DATA_BIND_OK);
        NativeHeaderMap_clear(&validation_owner);
      } else {
        check_equal(
            data_bind_native_clear(
                &options, map_data, &direct_map, sizeof(direct_map),
                &direct_diagnostic),
            DATA_BIND_OK);
      }
    }
    json_cserde_reader_destroy(map_reader);
    json_free(map_root);
    map_reader = NULL;
    map_root = NULL;

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
    /*
     * Canonical typed Map semantic zero retains its declared container and
     * key/value type identity. Only dynamic storage is released.
     */
    check_not_null(value.headers.cmeta.descriptor);
    check_true(cmeta_type_equal(
        value.headers.raw.key_type, SALTS_TSTR_CMETA_TYPE_REF));
    check_true(cmeta_type_equal(
        value.headers.raw.value_type, &NativeHeader_CMETA_TYPE));
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

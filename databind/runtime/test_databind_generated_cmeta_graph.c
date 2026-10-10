#include "cmeta_graph_generated.h"
#include "tinytest.h"

#include <cmeta_cmeta_data.h>
#include <cmeta_cmeta_fixed_width.h>

#include <stddef.h>
#include <string.h>

static DataBindStatus sample_parse_generated(
    DataBind *codec, DataBindFormat format,
    Sample_t *value, const char *input, size_t input_len,
    DataBindError *error) {
  switch (format) {
  case DATA_BIND_FORMAT_JSON:
    return Sample_from_json(codec, value, input, input_len, error);
  case DATA_BIND_FORMAT_YAML:
    return Sample_from_yaml(codec, value, input, input_len, error);
  case DATA_BIND_FORMAT_CSV:
    return Sample_from_csv(codec, value, input, input_len, 0u, error);
  case DATA_BIND_FORMAT_XML:
    return Sample_from_xml(codec, value, input, input_len, error);
  default:
    return DATA_BIND_ERR_INVALID_ARG;
  }
}

static DataBindStatus sample_serialize_generated(
    DataBind *codec, DataBindFormat format,
    const Sample_t *value, char **out, size_t *out_len,
    DataBindError *error) {
  switch (format) {
  case DATA_BIND_FORMAT_JSON:
    return Sample_to_json(codec, value, out, out_len, error);
  case DATA_BIND_FORMAT_YAML:
    return Sample_to_yaml(codec, value, out, out_len, error);
  case DATA_BIND_FORMAT_CSV:
    return Sample_to_csv(codec, value, out, out_len, error);
  case DATA_BIND_FORMAT_XML:
    return Sample_to_xml(codec, value, out, out_len, error);
  default:
    return DATA_BIND_ERR_INVALID_ARG;
  }
}

static void check_native_text_format_isolated(
    DataBindFormat format, const char *input, size_t input_len,
    const char *mapped_output, const char *canonical_output) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBind *codec = NULL;
  Sample_t value;
  char *serialized = NULL;
  size_t serialized_len = 0u;
  size_t allocated_before = 0u;
  size_t reused_before = 0u;
  size_t allocated_after_parse = 0u;
  size_t reused_after_parse = 0u;
  size_t allocated_after_serialize = 0u;
  size_t reused_after_serialize = 0u;
  DataBindStatus parse_status;
  DataBindStatus serialize_status = DATA_BIND_ERR_RUNTIME;
  int parsed_x;
  double parsed_y;
  State_t parsed_state;
  int32_t parsed_count;
  int has_mapped_output = 0;
  int has_canonical_output = 0;

  check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
  check_not_null(codec);
  if (codec == NULL) return;

  Sample_init(&value);
  data_bind_set_value_pool_enabled(0);
  data_bind_get_value_pool_stats(&allocated_before, &reused_before);
  parse_status = sample_parse_generated(
      codec, format, &value, input, input_len, &error);
  data_bind_get_value_pool_stats(&allocated_after_parse, &reused_after_parse);

  if (parse_status == DATA_BIND_OK)
    serialize_status = sample_serialize_generated(
        codec, format, &value, &serialized, &serialized_len, &error);
  data_bind_get_value_pool_stats(
      &allocated_after_serialize, &reused_after_serialize);

  parsed_x = value.point.x;
  parsed_y = value.point.y;
  parsed_state = value.state;
  parsed_count = value.count;
  if (serialized != NULL) {
    has_mapped_output = strstr(serialized, mapped_output) != NULL;
    has_canonical_output = strstr(serialized, canonical_output) != NULL;
  }
  data_bind_serialized_free(serialized);
  Sample_clear(&value);
  data_bind_free(codec);
  data_bind_set_value_pool_enabled(1);

  check_equal(parse_status, DATA_BIND_OK);
  check_equal(allocated_after_parse, allocated_before);
  check_equal(reused_after_parse, reused_before);
  check_equal(parsed_x, 3);
  check_equal(parsed_y, 4.5);
  check_equal(parsed_state, State_Ready);
  check_equal(parsed_count, 7);
  check_equal(serialize_status, DATA_BIND_OK);
  check_equal(allocated_after_serialize, allocated_after_parse);
  check_equal(reused_after_serialize, reused_after_parse);
  check(serialized_len != 0u);
  check(has_mapped_output);
  check(!has_canonical_output);
}

static size_t append_test_text(char *buffer, size_t capacity, size_t used,
                               const char *text) {
  size_t length = strlen(text);
  if (used > capacity || length >= capacity - used) return SIZE_MAX;
  memcpy(buffer + used, text, length + 1u);
  return used + length;
}

static size_t make_depth32_csv(char *buffer, size_t capacity) {
  size_t used = 0u;
  size_t depth;
  if (capacity != 0u) buffer[0] = '\0';
  for (depth = 0u; depth < 32u && used != SIZE_MAX; ++depth)
    used = append_test_text(buffer, capacity, used, "child.");
  if (used != SIZE_MAX)
    used = append_test_text(buffer, capacity, used, "value\r\n0\r\n");
  return used;
}

static size_t make_depth32_xml(char *buffer, size_t capacity) {
  size_t used = 0u;
  size_t depth;
  if (capacity != 0u) buffer[0] = '\0';
  used = append_test_text(buffer, capacity, used, "<Depth32>");
  for (depth = 0u; depth < 32u && used != SIZE_MAX; ++depth)
    used = append_test_text(buffer, capacity, used, "<child>");
  if (used != SIZE_MAX)
    used = append_test_text(buffer, capacity, used, "<value>0</value>");
  for (depth = 0u; depth < 32u && used != SIZE_MAX; ++depth)
    used = append_test_text(buffer, capacity, used, "</child>");
  if (used != SIZE_MAX)
    used = append_test_text(buffer, capacity, used, "</Depth32>");
  return used;
}

static DataBindStatus parse_depth32(DataBindFormat format, const char *input,
                                    size_t input_length) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBind *codec = NULL;
  Depth32_t value;
  Depth32_t unchanged;
  DataBindStatus status = Graph_codec_create(&codec, &error);
  memset(&value, 0, sizeof(value));
  memset(&unchanged, 0, sizeof(unchanged));
  if (status == DATA_BIND_OK) {
    if (format == DATA_BIND_FORMAT_CSV)
      status = Depth32_from_csv(codec, &value, input, input_length, 0u, &error);
    else if (format == DATA_BIND_FORMAT_XML)
      status = Depth32_from_xml(codec, &value, input, input_length, &error);
    else
      status = DATA_BIND_ERR_INVALID_ARG;
    if (status == DATA_BIND_ERR_SCHEMA &&
        memcmp(&value, &unchanged, sizeof(value)) != 0)
      status = DATA_BIND_ERR_RUNTIME;
  }
  Depth32_clear(&value);
  data_bind_free(codec);
  return status;
}

static DataBindStatus serialize_depth32(DataBindFormat format) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBind *codec = NULL;
  Depth32_t value;
  char *serialized = NULL;
  size_t serialized_length = sizeof(value);
  DataBindStatus status = Graph_codec_create(&codec, &error);
  memset(&value, 0, sizeof(value));
  if (status == DATA_BIND_OK) {
    if (format == DATA_BIND_FORMAT_CSV)
      status = Depth32_to_csv(codec, &value, &serialized, &serialized_length, &error);
    else if (format == DATA_BIND_FORMAT_XML)
      status = Depth32_to_xml(codec, &value, &serialized, &serialized_length, &error);
    else
      status = DATA_BIND_ERR_INVALID_ARG;
    if (status == DATA_BIND_ERR_SCHEMA &&
        (serialized != NULL || serialized_length != 0u))
      status = DATA_BIND_ERR_RUNTIME;
  }
  data_bind_serialized_free(serialized);
  data_bind_free(codec);
  return status;
}

static DataBindStatus parse_sample_count(
    DataBindFormat format, const char *input, size_t input_length,
    int32_t *out_count) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBind *codec = NULL;
  Sample_t value;
  DataBindStatus status = Graph_codec_create(&codec, &error);
  if (status != DATA_BIND_OK || codec == NULL) {
    data_bind_free(codec);
    return status != DATA_BIND_OK ? status : DATA_BIND_ERR_RUNTIME;
  }
  Sample_init(&value);
  status = sample_parse_generated(
      codec, format, &value, input, input_length, &error);
  if (status == DATA_BIND_OK && out_count != NULL)
    *out_count = value.count;
  Sample_clear(&value);
  data_bind_free(codec);
  return status;
}

spec("generated native CMeta graph") {
  it("releases nested fixed providers and owners independently of presence state") {
    enum { FIXED_OWNER_POISON = 0xa5, FIXED_OWNER_PAYLOAD_BYTES = 16 };
    FixedOwnedEnvelope_t value;
    const FixedOwnedEnvelope_t zero = {0};
    memset(&value, FIXED_OWNER_POISON, sizeof(value));
    FixedOwnedEnvelope_init(&value);
    check_null(value.child.label);
    check_equal(value.child.digest, zero.child.digest, sizeof(value.child.digest));
    check_equal(stl_byte_buffer_size(&value.payload), (size_t)0u);
    check_equal(value._presence[0], 0u);
    value.child.label = tstr_dup("fixed and owned");
    check_not_null(value.child.label);
    memset(value.child.digest, FIXED_OWNER_POISON, sizeof(value.child.digest));
    check_equal(stl_byte_buffer_resize(&value.payload, FIXED_OWNER_PAYLOAD_BYTES), STL_OK);
    check_equal(value._presence[0], 0u);
    FixedOwnedEnvelope_clear(&value);
    check_equal(&value, &zero, sizeof(value));
    FixedOwnedEnvelope_clear(&value);
    check_equal(&value, &zero, sizeof(value));
    value.child.label = tstr_dup("reuse fixed owner");
    check_not_null(value.child.label);
    value._presence[0] = 1u;
    FixedOwnedEnvelope_clear(&value);
    check_equal(&value, &zero, sizeof(value));
  }

  it("initializes and clears a scalar record beyond the published graph limit") {
    Depth33_t value;
    Depth33_t zero;
    memset(&zero, 0, sizeof(zero));
    memset(&value, 0xa5, sizeof(value));
    Depth33_init(&value);
    check_equal(memcmp(&value, &zero, sizeof(value)), 0);
    memset(&value, 0x5a, sizeof(value));
    Depth33_clear(&value);
    check_equal(memcmp(&value, &zero, sizeof(value)), 0);
    Depth33_clear(&value);
  }

  it("releases deeply nested owned fields and local state overlays") {
    OwnedDepth34_t value;
    const cmeta_data_desc *data = &cmeta_data_int32;
    DataBindError error = DATA_BIND_ERROR_INIT;
    OwnedDepth0_t *leaf = &value.child.child.child.child.child.child.child.child
        .child.child.child.child.child.child.child.child.child.child.child.child
        .child.child.child.child.child.child.child.child.child.child.child.child
        .child.child;
    memset(&value, 0xa5, sizeof(value));
    check_equal(OwnedDepth34_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check(data == &cmeta_data_int32);
    OwnedDepth34_init(&value);
    check_null(leaf->label);
    check_null(value.note);
    check_equal(leaf->value, 0);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
    if (leaf->label != NULL || value.note != NULL) return;
    check_equal(stl_byte_buffer_size(&leaf->payload), (size_t)0u);

    leaf->label = tstr_dup("deep owned string");
    value.note = tstr_dup("owned even when marked null");
    check_not_null(leaf->label);
    check_not_null(value.note);
    check_equal(stl_byte_buffer_resize(&leaf->payload, 16u), STL_OK);
    leaf->value = 7;
    value._nulls[0] = 1u;
    /* The absent/null overlays do not relinquish native field ownership. */
    OwnedDepth34_clear(&value);
    check_null(leaf->label);
    check_null(value.note);
    check_equal(stl_byte_buffer_size(&leaf->payload), (size_t)0u);
    check_equal(leaf->value, 0);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
    OwnedDepth34_clear(&value);

    OwnedDepth34_init(&value);
    leaf->label = tstr_dup("reuse after clear");
    check_not_null(leaf->label);
    check_equal(stl_byte_buffer_resize(&leaf->payload, 8u), STL_OK);
    OwnedDepth34_clear(&value);
    OwnedDepth34_init(NULL);
    OwnedDepth34_clear(NULL);
  }

  it("publishes structural CMeta and keeps wire facts in schema overlay") {
    const cmeta_data_desc *data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;

    check_equal(Sample_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (data != NULL) {
      const cmeta_data_struct_shape *shape = data->shape;
      const cmeta_data_struct_shape *point =
          shape != NULL && shape->field_count > 0u &&
                  shape->fields[0].value != NULL
              ? shape->fields[0].value->shape
              : NULL;
      const cmeta_data_enum_bits_ops *state =
          shape != NULL && shape->field_count > 1u &&
                  shape->fields[1].value != NULL
              ? cmeta_data_enum_bits_ops_of(shape->fields[1].value)
              : NULL;

      check(cmeta_data_desc_valid(data));
      check_equal(data->kind, CMETA_DATA_STRUCT);
      check_equal(data->storage_type->size, sizeof(Sample_t));
      check_equal(data->storage_type->align, _Alignof(Sample_t));
      check_not_null(shape);
      if (shape == NULL) return;
      check_equal(shape->field_count, 3u);
      check_equal(shape->fields[0].offset, offsetof(Sample_t, point));
      check_equal(shape->fields[1].offset, offsetof(Sample_t, state));
      check_not_null(point);
      if (point == NULL) return;
      check_equal(point->field_count, 2u);
      check_equal(point->fields[1].offset, offsetof(Point_t, y));
      check(cmeta_type_equal(point->fields[0].value->storage_type,
                             cmeta_data_int32.storage_type));
      check_equal(shape->fields[1].value->kind, CMETA_DATA_ENUM);
      check_equal(shape->fields[1].value->storage_type->size, sizeof(State_t));
      check_not_null(state);
      if (state == NULL) return;
      check_null(shape->fields[1].value->shape);
      check_null(shape->fields[1].value->enum_ops);
      check_equal(state->domain->count, 2u);
      check_equal(state->domain->items[1].bits, 7u);
      check_equal(state->domain->items[1].symbol, "Ready");
      check_equal(shape->fields[2].name, "count");
      check_null(cmeta_data_struct_find_field(shape, "wire_count"));
      check_null(cmeta_data_struct_find_field(shape, "old_count"));
    }

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check(data_bind_schema_field_at(codec, "Sample", 2u, &schema_field));
      check_equal(schema_field.name, "count");
      check(schema_field.has_default);
      check_equal(schema_field.default_value, "9");
      check_equal(schema_field.offset, 14u);
      check(schema_field.has_cmeta_kind);
      check_equal(schema_field.cmeta_kind, CMETA_DATA_SINT);
      data_bind_free(codec);
    }
  }

  it("keeps generated native JSON isolated from dynamic values") {
    static const char json[] =
        "{\"point\":{\"x\":3,\"y\":4.5},\"state\":7,\"wire_count\":7}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    Sample_t value;
    char *serialized = NULL;
    size_t serialized_len = 0u;
    size_t allocated_before = 0u;
    size_t reused_before = 0u;
    size_t allocated_after = 0u;
    size_t reused_after = 0u;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;
    Sample_init(&value);
    data_bind_set_value_pool_enabled(0);
    data_bind_get_value_pool_stats(&allocated_before, &reused_before);

    check_equal(
        Sample_from_json(
            codec, &value, json, sizeof(json) - 1u, &error),
        DATA_BIND_OK);
    data_bind_get_value_pool_stats(&allocated_after, &reused_after);
    check_equal(allocated_after, allocated_before);
    check_equal(reused_after, reused_before);
    check_equal(value.point.x, 3);
    check_equal(value.point.y, 4.5);
    check_equal(value.state, State_Ready);
    check_equal(value.count, 7);

    allocated_before = allocated_after;
    reused_before = reused_after;
    check_equal(
        Sample_to_json(
            codec, &value, &serialized, &serialized_len, &error),
        DATA_BIND_OK);
    data_bind_get_value_pool_stats(&allocated_after, &reused_after);
    check_equal(allocated_after, allocated_before);
    check_equal(reused_after, reused_before);
    check_not_null(serialized);
    check(serialized_len != 0u);
    if (serialized != NULL) {
      check_not_null(strstr(serialized, "\"wire_count\":7"));
      check_null(strstr(serialized, "\"count\":7"));
    }

    data_bind_serialized_free(serialized);
    Sample_clear(&value);
    data_bind_free(codec);
    data_bind_set_value_pool_enabled(1);
  }

  it("keeps generated native YAML isolated from dynamic values") {
    static const char yaml[] =
        "\"point\":\n"
        "  \"x\": 3\n"
        "  \"y\": 4.5\n"
        "\"state\": 7\n"
        "\"wire_count\": 7\n";

    check_native_text_format_isolated(
        DATA_BIND_FORMAT_YAML, yaml, sizeof(yaml) - 1u,
        "wire_count: 7", "\ncount: 7");
  }

  it("fails closed for nested Sample CSV without canonical flat admission") {
    static const char csv[] =
        "point.x,point.y,state,wire_count\r\n"
        "3,4.5,7,7\r\n";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    Sample_t value;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;
    Sample_init(&value);
    check_equal(
        Sample_from_csv(
            codec, &value, csv, sizeof(csv) - 1u, 0u, &error),
        DATA_BIND_ERR_SCHEMA);
    Sample_clear(&value);
    data_bind_free(codec);
  }

  it("round trips nested Sample XML with root aliases and defaults") {
    static const char xml[] =
        "<Sample><point><x>3</x><y>4.5</y></point><state>7</state>"
        "<old_count>7</old_count></Sample>";
    static const char missing_default[] =
        "<Sample><point><x>-3</x><y>2.5</y></point><state>0</state></Sample>";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    Sample_t value, roundtrip;
    char *output = NULL;
    size_t length = 0u;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    Sample_init(&value);
    Sample_init(&roundtrip);
    {
      DataBindStatus status = Sample_from_xml(codec, &value, xml, sizeof(xml) - 1u, &error);
      info("nested XML status=%d path=%s message=%s", status, error.path, error.message);
      check_equal(status, DATA_BIND_OK);
    }
    check_equal(value.point.x, 3);
    check_equal(value.point.y, 4.5);
    check_equal(value.count, 7);
    check_equal(Sample_to_xml(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_contains(output, "<point><x>3</x><y>4.5</y></point>");
    check_contains(output, "<wire_count>7</wire_count>");
    check_equal(Sample_from_xml(codec, &roundtrip, output, length, &error), DATA_BIND_OK);
    check_equal(roundtrip.point.x, value.point.x);
    check_equal(roundtrip.point.y, value.point.y);
    check_equal(roundtrip.state, value.state);
    check_equal(roundtrip.count, value.count);
    data_bind_serialized_free(output);
    check_equal(Sample_from_xml(codec, &value, missing_default,
                               sizeof(missing_default) - 1u, &error), DATA_BIND_OK);
    check_equal(value.count, 9);
    Sample_clear(&roundtrip);
    Sample_clear(&value);
    data_bind_free(codec);
  }

  it("rejects invalid nested XML without publishing partial fields") {
    static const char *const invalid[] = {
        "<Sample><point><x>3</x><y>bad</y></point><state>7</state></Sample>",
        "<Sample><point><x>2147483648</x><y>1</y></point><state>7</state></Sample>",
        "<Sample><point><x>3</x><x>4</x><y>1</y></point><state>7</state></Sample>",
        "<Sample><point><x>3</x></point><state>7</state></Sample>",
        "<Sample><point><x>3</x><y>1</y><unknown>1</unknown></point><state>7</state></Sample>"
    };
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    Sample_t value;
    size_t i;
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    Sample_init(&value);
    value.point.x = 91;
    value.point.y = 9.5;
    value.count = 92;
    for (i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
      check_not_equal(Sample_from_xml(codec, &value, invalid[i], strlen(invalid[i]), &error), DATA_BIND_OK);
      check_equal(value.point.x, 91);
      check_equal(value.point.y, 9.5);
      check_equal(value.count, 92);
    }
    Sample_clear(&value);
    data_bind_free(codec);
  }

  it("round trips three XML record levels with nested aliases exact integers and owned text") {
    static const char xml[] =
        "<XmlEnvelope><body><old_leaf><old_low>-9223372036854775808</old_low>"
        "<high>18446744073709551615</high><enabled>yes</enabled><score>1.25</score>"
        "<old_label>A&amp;B</old_label><state>Ready</state><signed_value>-9223372036854775808</signed_value>"
        "<wide>18446744073709551615</wide><permissions>3</permissions>"
        "</old_leaf></body><tail>17</tail></XmlEnvelope>";
    static const char invalid[] =
        "<XmlEnvelope><body><leaf><low>-1</low><high>2</high><enabled>true</enabled>"
        "<score>2.5</score><label>temporary</label><state>Ready</state>"
        "<signed_value>-9223372036854775808</signed_value><wide>0</wide><permissions>1</permissions>"
        "</leaf></body><tail>bad</tail></XmlEnvelope>";
    static const char duplicate[] =
        "<XmlEnvelope><body><old_leaf><wire_label>temporary</wire_label>"
        "<old_label>duplicate</old_label></old_leaf></body><tail>0</tail></XmlEnvelope>";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    XmlEnvelope_t value, roundtrip;
    char *output = NULL;
    size_t length = 0u;
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    XmlEnvelope_init(&value);
    XmlEnvelope_init(&roundtrip);
    {
      DataBindStatus status = XmlEnvelope_from_xml(codec, &value, xml, sizeof(xml) - 1u, &error);
      info("nested XML status=%d path=%s message=%s", status, error.path, error.message);
      check_equal(status, DATA_BIND_OK);
    }
    check_equal(value.body.leaf.low, INT64_MIN);
    check_equal(value.body.leaf.high, UINT64_MAX);
    check(value.body.leaf.enabled);
    check_equal(value.body.leaf.score, 1.25);
    check_equal(value.body.leaf.label, "A&B");
    check_equal(XmlEnvelope_to_xml(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_contains(output, "<wire_label>A&amp;B</wire_label>");
    check_contains(output, "<wire_leaf>");
    check_contains(output, "<wire_low>-9223372036854775808</wire_low>");
    check_null(strstr(output, "old_"));
    check_equal(XmlEnvelope_from_xml(codec, &roundtrip, output, length, &error), DATA_BIND_OK);
    check_equal(roundtrip.body.leaf.low, INT64_MIN);
    check_equal(roundtrip.body.leaf.high, UINT64_MAX);
    check_equal(roundtrip.body.leaf.label, "A&B");
    check_equal(roundtrip.tail, 17);
    check_equal(roundtrip.body.leaf.enabled, (uint8_t)1u);
    check_equal(roundtrip.body.leaf.score, 1.25);
    check_equal(roundtrip.body.leaf.state, (int16_t)7);
    check_equal(roundtrip.body.leaf.signed_value, INT64_MIN);
    check_equal(roundtrip.body.leaf.wide, UINT64_MAX);
    check_equal(roundtrip.body.leaf.permissions, (uint8_t)3u);
    check_equal(XmlEnvelope_from_xml(codec, &value, invalid, sizeof(invalid) - 1u, &error), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(value.body.leaf.low, INT64_MIN);
    check_equal(value.body.leaf.label, "A&B");
    check_equal(value.tail, 17);
    check_equal(XmlEnvelope_from_xml(codec, &value, duplicate, sizeof(duplicate) - 1u, &error),
                DATA_BIND_ERR_PARSE);
    check_equal(error.path, "body");
    check_contains(error.message, "Duplicate native Struct field");
    check_equal(value.body.leaf.label, "A&B");
    check_equal(value.tail, 17);
    data_bind_serialized_free(output);
    output = NULL;
    check_equal(XmlEnvelope_to_json(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_contains(output, "\"wire_leaf\"");
    check_contains(output, "\"wire_label\"");
    check_equal(XmlEnvelope_from_json(codec, &roundtrip, output, length, &error), DATA_BIND_OK);
    check_equal(roundtrip.body.leaf.label, "A&B");
    data_bind_serialized_free(output);
    output = NULL;
    check_equal(XmlEnvelope_to_yaml(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_equal(XmlEnvelope_from_yaml(codec, &roundtrip, output, length, &error), DATA_BIND_OK);
    check_equal(roundtrip.body.leaf.enabled, (uint8_t)1u);
    check_equal(roundtrip.body.leaf.high, UINT64_MAX);
    check_equal(roundtrip.body.leaf.label, "A&B");
    data_bind_serialized_free(output);
    XmlEnvelope_clear(&roundtrip);
    XmlEnvelope_clear(&value);
    data_bind_free(codec);
  }

  it("round trips nested YAML field names and input aliases") {
    static const char yaml[] = "first:\n  old_value: 7\nsecond:\n  value: -9\n";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    XmlNamedPair_t value, roundtrip;
    char *output = NULL;
    size_t length = 0u;
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    XmlNamedPair_init(&value);
    XmlNamedPair_init(&roundtrip);
    check_equal(XmlNamedPair_from_yaml(codec, &value, yaml, sizeof(yaml) - 1u, &error), DATA_BIND_OK);
    check_equal(value.first.value, 7);
    check_equal(value.second.value, -9);
    check_equal(XmlNamedPair_to_yaml(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_contains(output, "wire_value: 7");
    check_contains(output, "wire_value: -9");
    check_null(strstr(output, "old_value"));
    check_equal(XmlNamedPair_from_yaml(codec, &roundtrip, output, length, &error), DATA_BIND_OK);
    check_equal(roundtrip.first.value, 7);
    check_equal(roundtrip.second.value, -9);
    data_bind_serialized_free(output);
    XmlNamedPair_clear(&roundtrip);
    XmlNamedPair_clear(&value);
    data_bind_free(codec);
  }

  it("preserves record collection reflection and independent owners through the C facade") {
    static const char xml[] =
        "<XmlSequenceItem><id>7</id><wire_text>A&amp;B</wire_text></XmlSequenceItem>";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    XmlSequenceItem_t source, copied;
    XmlSequences_items_vec_t items = {0};
    const cmeta_data_desc *data = XmlSequences_items_vec_t_cmeta_data();
    const XmlSequenceItem_t *stored;
    cmeta_range range;
    cmeta_range_cursor cursor = {0};
    cmeta_collector collector = XmlSequences_items_vec_t_collector(&items, 1u);

    /* Object-independent metadata is required before a collection exists. */
    check(cmeta_data_desc_equal(cmeta_data_collection_element_data(data),
                                &XmlSequenceItem_CMETA_DATA));
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    if (codec == NULL) return;
    XmlSequenceItem_init(&source);
    XmlSequenceItem_init(&copied);
    check_equal(XmlSequenceItem_from_xml(codec, &source, xml, sizeof(xml) - 1u,
                                         &error), DATA_BIND_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    check_equal(cmeta_collector_accept(&collector, &XmlSequenceItem_CMETA_TYPE,
                                       &source), CMETA_OK);
    check_equal(cmeta_collector_finish(&collector), CMETA_OK);
    stored = XmlSequences_items_vec_t_at_const(&items, 0u);
    check_not_null(stored);
    if (stored != NULL) {
      check(stored->text != source.text);
      check_equal(stored->text, "A&B");
    }
    XmlSequenceItem_clear(&source);
    data_bind_free(codec);

    range = XmlSequences_items_vec_t_range(&items);
    check(range.flags & CMETA_RANGE_CONSTRUCTS_VALUES);
    check_equal(cmeta_range_next(&range, &cursor, &copied), CMETA_GEN_VALUE_AND_DONE);
    check_equal(copied.id, 7);
    check_equal(copied.text, "A&B");
    if (stored != NULL) check(copied.text != stored->text);
    XmlSequences_items_vec_t_destroy(&items);
    /* Range materialization owns its copy after the source container dies. */
    check_equal(copied.text, "A&B");
    XmlSequenceItem_clear(&copied);
  }

  it("round trips repeated XML sequences with interleaved aliases and nested owned records") {
    static const char xml[] =
        "<XmlSequenceEnvelope><data><old_values>1</old_values>"
        "<items><id>7</id><old_text>A&amp;B</old_text></items><marker>9</marker>"
        "<values>-2</values><tags>x</tags><switches>true</switches>"
        "<wire_values>3</wire_values><items><id>8</id><text>second</text></items>"
        "<tags>y</tags><switches>false</switches></data><title>ok</title></XmlSequenceEnvelope>";
    static const char empty[] =
        "<XmlSequenceEnvelope><data><marker>0</marker></data><title>empty</title></XmlSequenceEnvelope>";
    static const char invalid[] =
        "<XmlSequenceEnvelope><data><marker>2</marker>"
        "<items><id>1</id><text>temporary</text></items>"
        "<items><id>bad</id><text>bad</text></items></data><title>bad</title></XmlSequenceEnvelope>";
    static const char *const rejected[] = {
        "<XmlSequenceEnvelope><data><marker>1</marker><unknown>2</unknown></data><title>x</title></XmlSequenceEnvelope>",
        "<XmlSequenceEnvelope><data><marker>1</marker><marker>2</marker></data><title>x</title></XmlSequenceEnvelope>",
        "<XmlSequenceEnvelope><data><marker>1</marker><items><id>1</id><wire_text>x</wire_text><old_text>y</old_text></items></data><title>x</title></XmlSequenceEnvelope>",
        "<XmlSequenceEnvelope><data><marker>1</marker><old_values>2147483648</old_values></data><title>x</title></XmlSequenceEnvelope>",
        "<XmlSequenceEnvelope><data old_values=\"1\"><marker>1</marker></data><title>x</title></XmlSequenceEnvelope>"
    };
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    XmlSequenceEnvelope_t value, decoded;
    char *output = NULL;
    size_t length = 0u;
    size_t i;
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    XmlSequenceEnvelope_init(&value);
    XmlSequenceEnvelope_init(&decoded);
    {
      DataBindStatus status = XmlSequenceEnvelope_from_xml(codec, &value, xml, sizeof(xml) - 1u, &error);
      info("XML sequences status=%d path=%s message=%s", status, error.path, error.message);
      check_equal(status, DATA_BIND_OK);
    }
    check_equal(XmlSequences_values_vec_t_size(&value.data.values), (size_t)3u);
    check_equal(*XmlSequences_values_vec_t_at_const(&value.data.values, 1u), -2);
    check_equal(XmlSequences_items_vec_t_size(&value.data.items), (size_t)2u);
    check_equal(XmlSequences_items_vec_t_at_const(&value.data.items, 0u)->text, "A&B");
    check_equal(XmlSequences_switches_vec_t_size(&value.data.switches), (size_t)2u);
    check_equal(XmlSequences_tags_set_t_size(&value.data.tags), (size_t)2u);
    check_equal(*XmlSequences_switches_vec_t_at_const(&value.data.switches, 1u), (uint8_t)0u);
    check_equal(XmlSequenceEnvelope_to_xml(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_contains(output, "<wire_values>1</wire_values><wire_values>-2</wire_values><wire_values>3</wire_values>");
    check_contains(output, "<wire_text>A&amp;B</wire_text>");
    check_contains(output, "<tags>x</tags>");
    check_contains(output, "<tags>y</tags>");
    check_equal(XmlSequenceEnvelope_from_xml(codec, &decoded, output, length, &error), DATA_BIND_OK);
    check_equal(XmlSequences_items_vec_t_at_const(&decoded.data.items, 1u)->text, "second");
    check_equal(decoded.title, "ok");
    data_bind_serialized_free(output);
    output = NULL;
    check_equal(XmlSequenceEnvelope_to_json(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_contains(output, "\"wire_values\"");
    check_contains(output, "\"wire_text\"");
    check_equal(XmlSequenceEnvelope_from_json(codec, &decoded, output, length, &error), DATA_BIND_OK);
    check_equal(XmlSequences_items_vec_t_at_const(&decoded.data.items, 0u)->text, "A&B");
    data_bind_serialized_free(output);
    output = NULL;
    check_equal(XmlSequenceEnvelope_to_yaml(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_equal(XmlSequenceEnvelope_from_yaml(codec, &decoded, output, length, &error), DATA_BIND_OK);
    check_equal(*XmlSequences_switches_vec_t_at_const(&decoded.data.switches, 0u), (uint8_t)1u);
    check_equal(*XmlSequences_switches_vec_t_at_const(&decoded.data.switches, 1u), (uint8_t)0u);
    data_bind_serialized_free(output);
    output = NULL;
    check_not_equal(XmlSequenceEnvelope_from_xml(codec, &value, invalid, sizeof(invalid) - 1u, &error), DATA_BIND_OK);
    check_equal(value.data.marker, 9);
    check_equal(XmlSequences_items_vec_t_at_const(&value.data.items, 0u)->text, "A&B");
    check_equal(value.title, "ok");
    for (i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      check_not_equal(XmlSequenceEnvelope_from_xml(codec, &value, rejected[i], strlen(rejected[i]), &error), DATA_BIND_OK);
      check_equal(value.data.marker, 9);
      check_equal(XmlSequences_items_vec_t_at_const(&value.data.items, 0u)->text, "A&B");
      check_equal(value.title, "ok");
    }
    check_equal(XmlSequenceEnvelope_from_xml(codec, &value, empty, sizeof(empty) - 1u, &error), DATA_BIND_OK);
    check_equal(XmlSequences_values_vec_t_size(&value.data.values), (size_t)0u);
    check_equal(XmlSequences_items_vec_t_size(&value.data.items), (size_t)0u);
    check_equal(XmlSequenceEnvelope_to_xml(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_null(strstr(output, "wire_values"));
    check_equal(XmlSequenceEnvelope_from_xml(codec, &decoded, output, length, &error), DATA_BIND_OK);
    check_equal(XmlSequences_values_vec_t_size(&decoded.data.values), (size_t)0u);
    check_equal(decoded.title, "empty");
    data_bind_serialized_free(output);
    XmlSequenceEnvelope_clear(&decoded);
    XmlSequenceEnvelope_clear(&value);
    data_bind_free(codec);
  }

  it("rejects non-integral JSON tokens for native integer storage") {
    static const char json[] =
        "{\"point\":{\"x\":3.0,\"y\":4.5},\"state\":7e0}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    Sample_t value;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    Sample_init(&value);
    if (codec != NULL)
      check_equal(Sample_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(value.point.x, 0);
    check_equal(value.state, State_Idle);
    check_equal(value.count, 0);
    Sample_clear(&value);
    data_bind_free(codec);
  }

  it("rejects textual Boolean coercion through generated JSON") {
    static const char json[] = "{\"value\":\"yes\"}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    BoolStorage_t value;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    BoolStorage_init(&value);
    if (codec != NULL)
      check_equal(BoolStorage_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(value.value, 0u);
    BoolStorage_clear(&value);
    data_bind_free(codec);
  }

  it("rejects unsupported Boolean defaults for integral native fields") {
    static const char json[] = "{}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    IntegerBoolDefaults_t value;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    IntegerBoolDefaults_init(&value);
    if (codec != NULL)
      check_equal(IntegerBoolDefaults_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_ERR_SCHEMA);
    check_equal(value.enabled, 0u);
    check_equal(value.debug, 0);
    IntegerBoolDefaults_clear(&value);
    data_bind_free(codec);
  }

  it("rejects delimited flags coercion through generated JSON") {
    static const char json[] = "{\"value\":\"Read|Write\"}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    FlagStorage_t value;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    FlagStorage_init(&value);
    if (codec != NULL)
      check_equal(FlagStorage_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(value.value, 0u);
    FlagStorage_clear(&value);
    data_bind_free(codec);
  }

  it("rejects unsupported nested native CSV with an invalid scalar") {
    static const char csv[] =
        "point.x,point.y,state,wire_count\r\n"
        "3,4.5,7,garbage\r\n";
    int32_t count = 0;
    check_equal(parse_sample_count(DATA_BIND_FORMAT_CSV, csv,
                                   sizeof(csv) - 1u, &count),
                DATA_BIND_ERR_SCHEMA);
    check_equal(count, 0);
  }

  it("rejects nested native XML with an invalid scalar") {
    static const char xml[] =
        "<Sample><point><x>3</x><y>4.5</y></point><state>7</state>"
        "<wire_count>garbage</wire_count></Sample>";
    int32_t count = 0;
    check_equal(parse_sample_count(DATA_BIND_FORMAT_XML, xml,
                                   sizeof(xml) - 1u, &count),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(count, 0);
  }

  it("keeps invalid scalar defaults strict in native JSON and YAML") {
    static const char json[] =
        "{\"point\":{\"x\":3,\"y\":4.5},\"state\":7,"
        "\"wire_count\":\"garbage\"}";
    static const char yaml[] =
        "\"point\":\n"
        "  \"x\": 3\n"
        "  \"y\": 4.5\n"
        "\"state\": 7\n"
        "\"wire_count\": garbage\n";
    check_equal(parse_sample_count(DATA_BIND_FORMAT_JSON, json,
                                   sizeof(json) - 1u, NULL),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(parse_sample_count(DATA_BIND_FORMAT_YAML, yaml,
                                   sizeof(yaml) - 1u, NULL),
                DATA_BIND_ERR_TYPE_MISMATCH);
  }

  it("rejects generated Depth32 CSV parsing without a canonical provider") {
    char csv[512];
    size_t length = make_depth32_csv(csv, sizeof(csv));
    check(length != SIZE_MAX);
    if (length != SIZE_MAX)
      check_equal(parse_depth32(DATA_BIND_FORMAT_CSV, csv, length),
                  DATA_BIND_ERR_SCHEMA);
  }

  it("rejects generated Depth32 CSV output without a canonical provider") {
    check_equal(serialize_depth32(DATA_BIND_FORMAT_CSV), DATA_BIND_ERR_SCHEMA);
  }

  it("rejects generated Depth32 XML parsing without a canonical provider") {
    char xml[768];
    size_t length = make_depth32_xml(xml, sizeof(xml));
    check(length != SIZE_MAX);
    if (length != SIZE_MAX)
      check_equal(parse_depth32(DATA_BIND_FORMAT_XML, xml, length),
                  DATA_BIND_ERR_SCHEMA);
  }

  it("rejects generated Depth32 XML output without a canonical provider") {
    check_equal(serialize_depth32(DATA_BIND_FORMAT_XML), DATA_BIND_ERR_SCHEMA);
  }

  it("publishes canonical CMeta for owned, optional, and nested storage") {
    const cmeta_data_desc *sentinel = &cmeta_data_int32;
    const cmeta_data_desc *data = sentinel;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(Unsupported_cmeta_data(&data, &error), DATA_BIND_OK);
    check(data != sentinel);
    check(cmeta_data_desc_valid(data));
    data = sentinel;
    check_equal(OptionalStorage_cmeta_data(&data, &error), DATA_BIND_OK);
    check(data != sentinel);
    check(cmeta_data_desc_valid(data));
    data = sentinel;
    check_equal(UnsupportedNested_cmeta_data(&data, &error), DATA_BIND_OK);
    check(data != sentinel);
    check(cmeta_data_desc_valid(data));
  }

#ifndef TBE_CMETA_NODE_FRONTEND_SMOKE
  it("keeps defaults and wire aliases in the schema overlay") {
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_desc *data = NULL;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(Sample_cmeta_data(&data, &error), DATA_BIND_OK);
    if (codec && data) {
      check(data_bind_schema_field_at(codec, "Sample", 2u, &field));
      check(field.has_default);
      check_equal(field.default_value, "9");
      check_equal(field.name, "count");
      check_equal(field.offset, 14u);
      check(field.has_cmeta_kind);
      check_equal(field.cmeta_kind, CMETA_DATA_SINT);
      {
        const cmeta_data_struct_shape *shape = data->shape;
        cmeta_type_desc copied = *shape->fields[2].value->storage_type;
        cmeta_type_identity identity = *copied.identity;
        copied.identity = &identity;
        check_not_null(field.cmeta_data);
        check(cmeta_type_equal(field.cmeta_data->storage_type, &copied));
        check_equal(field.cmeta_data->stable_id,
                    shape->fields[2].value->stable_id);
        check(data_bind_schema_field_at(codec, "Sample", 0u, &field));
        check_equal(field.cmeta_kind, CMETA_DATA_STRUCT);
        check_null(field.cmeta_data);
        check(data_bind_schema_field_at(codec, "Sample", 1u, &field));
        check_equal(field.cmeta_kind, CMETA_DATA_ENUM);
        check_null(field.cmeta_data);
      }
      data_bind_free(codec);
    }
  }
#endif

  it("publishes depth 32 and rejects depth 33 without partial publication") {
    const cmeta_data_desc *data = NULL;
    const cmeta_data_desc *published;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(Depth32_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    published = data;
    check_equal(Depth33_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check(data == published);
    check_equal(error.code, DATA_BIND_ERR_SCHEMA);
    check_equal(error.path, "Depth33");
    check_not_null(strstr(error.message, "CMeta"));
  }

  it("publishes generated uint8 boolean storage through canonical Bool8") {
    const cmeta_data_desc *data = &cmeta_data_int32;
    const cmeta_data_desc *native_bool;
    const cmeta_data_struct_shape *shape;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check(_Generic(((BoolStorage_t *)0)->value, uint8_t: 1, default: 0));
    check_equal(BoolStorage_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (!data) return;
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_not_null(shape);
    if (!shape || shape->field_count != 1u) return;
    native_bool = shape->fields[0].value;
    check_not_null(native_bool);
    if (!native_bool) return;
    check_equal(native_bool->kind, CMETA_DATA_BOOL);
    check(cmeta_type_equal(native_bool->storage_type,
                           &cmeta_bool8_cmeta_type));
    check_not_null(cmeta_data_fixed_ops_of(native_bool));
    check(!cmeta_type_equal(native_bool->storage_type,
                            cmeta_data_bool.storage_type));
  }

  it("admits generated Bool8 input while preserving fixed-value output limits") {
    static const char json[] =
        "{\"enabled\":true,\"id\":\"00000000-0000-0000-0000-000000000000\","
        "\"digest\":\"0123456789abcdef\"}";
    const cmeta_data_desc *native = NULL;
    const cmeta_data_struct_shape *shape;
    const cmeta_struct_desc *layout;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    FixedValues_t destination;
    uint8_t *wire = NULL;
    char *encoded = NULL;
    size_t encoded_len = 0u;
    size_t wire_len = 0u;
    size_t fixed_extent = 0u;

    check_equal(FixedValues_cmeta_data(&native, &error), DATA_BIND_OK);
    check_not_null(native);
    if (!native || !native->shape) return;
    shape = (const cmeta_data_struct_shape *)native->shape;
    layout = shape->layout;
    check_not_null(layout);
    if (!layout || shape->field_count != 3u) return;

    check_equal(shape->fields[0].value->kind, CMETA_DATA_BOOL);
    check_equal(shape->fields[0].value->storage_type->size,
                sizeof(((FixedValues_t *)0)->enabled));
    check_equal(shape->fields[0].value->storage_type->align,
                _Alignof(uint8_t));
    check_not_null(cmeta_data_fixed_ops_of(shape->fields[0].value));
    check_equal(cmeta_data_fixed_extent(shape->fields[0].value, &fixed_extent),
                CMETA_OK);
    check_equal(fixed_extent, sizeof(((FixedValues_t *)0)->enabled));
    check(cmeta_uuid_cmeta_data_valid(shape->fields[1].value));
    check_equal(shape->fields[1].value->storage_type->size,
                sizeof(((FixedValues_t *)0)->id));
    check_equal(shape->fields[1].value->storage_type->align,
                _Alignof(cmeta_uuid_t));
    check_equal(cmeta_data_fixed_extent(shape->fields[1].value, &fixed_extent),
                CMETA_OK);
    check_equal(fixed_extent, sizeof(((FixedValues_t *)0)->id));
    check_equal(shape->fields[2].value->kind, CMETA_DATA_BYTES);
    check_equal(shape->fields[2].value->storage_type->size,
                sizeof(((FixedValues_t *)0)->digest));
    check_equal(layout->fields[2].size,
                sizeof(((FixedValues_t *)0)->digest));
    check_equal(cmeta_data_fixed_extent(shape->fields[2].value, &fixed_extent),
                CMETA_OK);
    check_equal(fixed_extent, sizeof(((FixedValues_t *)0)->digest));

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    if (!codec) return;
    FixedValues_init(&destination);
    check_equal(FixedValues_from_json(
                    codec, &destination, json, strlen(json), &error), DATA_BIND_OK);
    check_equal(destination.enabled, (uint8_t)1u);
    check(memcmp(destination.digest, "0123456789abcdef", sizeof(destination.digest)) == 0);
    {
      DataBindStatus status = FixedValues_to_json(
          codec, &destination, &encoded, &encoded_len, &error);
      info("fixed-value JSON output: %s (%s)", error.message, error.path);
      check_equal(status, DATA_BIND_ERR_SCHEMA);
      check_null(encoded);
      check_equal(encoded_len, (size_t)0u);
    }
    /* Bool8 now admits, while unsupported fixed-value egress still fails
     * before publishing an output buffer. */
    check_equal(FixedValues_to_bin(
                    codec, &destination, &wire, &wire_len, &error), DATA_BIND_ERR_SCHEMA);
    check_null(wire);
    check_equal(wire_len, (size_t)0u);

    data_bind_serialized_free(encoded);
    FixedValues_clear(&destination);
    check(memcmp(&destination, &(FixedValues_t){0}, sizeof(destination)) == 0);
    data_bind_free(codec);
  }

  it("publishes exact signed and unsigned enum storage domains") {
    const cmeta_data_desc *signed8 = NULL;
    const cmeta_data_desc *unsigned8 = NULL;
    const cmeta_data_desc *signed64 = NULL;
    const cmeta_data_struct_shape *signed8_shape;
    const cmeta_data_struct_shape *unsigned8_shape;
    const cmeta_data_struct_shape *signed64_shape;
    const cmeta_data_desc *signed8_value;
    const cmeta_data_desc *unsigned8_value;
    const cmeta_data_desc *signed64_value;
    Signed8Domain_t signed8_storage = 0;
    Unsigned8Domain_t unsigned8_storage = 0;
    Signed64Domain_t signed64_storage = 0;
    uint64_t value = 0;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(Signed8Storage_cmeta_data(&signed8, &error), DATA_BIND_OK);
    check_equal(Unsigned8Storage_cmeta_data(&unsigned8, &error), DATA_BIND_OK);
    check_equal(Signed64Storage_cmeta_data(&signed64, &error), DATA_BIND_OK);
    check_not_null(signed8);
    check_not_null(unsigned8);
    check_not_null(signed64);
    if (!signed8 || !unsigned8 || !signed64) return;

    signed8_shape = (const cmeta_data_struct_shape *)signed8->shape;
    unsigned8_shape = (const cmeta_data_struct_shape *)unsigned8->shape;
    signed64_shape = (const cmeta_data_struct_shape *)signed64->shape;
    check_not_null(signed8_shape);
    check_not_null(unsigned8_shape);
    check_not_null(signed64_shape);
    if (!signed8_shape || !unsigned8_shape || !signed64_shape) return;
    check_equal(signed8_shape->field_count, 1u);
    check_equal(unsigned8_shape->field_count, 1u);
    check_equal(signed64_shape->field_count, 1u);
    if (signed8_shape->field_count != 1u ||
        unsigned8_shape->field_count != 1u ||
        signed64_shape->field_count != 1u)
      return;
    check_not_null(signed8_shape->fields);
    check_not_null(unsigned8_shape->fields);
    check_not_null(signed64_shape->fields);
    if (!signed8_shape->fields || !unsigned8_shape->fields ||
        !signed64_shape->fields)
      return;
    signed8_value = signed8_shape->fields[0].value;
    unsigned8_value = unsigned8_shape->fields[0].value;
    signed64_value = signed64_shape->fields[0].value;
    check_not_null(signed8_value);
    check_not_null(unsigned8_value);
    check_not_null(signed64_value);
    if (!signed8_value || !unsigned8_value || !signed64_value) return;
    check_equal(signed8_value->storage_type->size, sizeof(int8_t));
    check_equal(unsigned8_value->storage_type->size, sizeof(uint8_t));
    check_equal(signed64_value->storage_type->size, sizeof(int64_t));
    check_not_null(cmeta_data_enum_bits_ops_of(signed8_value));
    check_not_null(cmeta_data_enum_bits_ops_of(unsigned8_value));
    check_not_null(cmeta_data_enum_bits_ops_of(signed64_value));
    if (!cmeta_data_enum_bits_ops_of(signed8_value) ||
        !cmeta_data_enum_bits_ops_of(unsigned8_value) ||
        !cmeta_data_enum_bits_ops_of(signed64_value)) return;
    check_equal(signed8_value->enum_bits_ops->domain->signedness, CMETA_ENUM_SIGNED);
    check_equal(signed8_value->enum_bits_ops->domain->bits, 8u);
    check_equal(unsigned8_value->enum_bits_ops->domain->signedness, CMETA_ENUM_UNSIGNED);
    check_equal(unsigned8_value->enum_bits_ops->domain->bits, 8u);
    check_equal(signed64_value->enum_bits_ops->domain->signedness, CMETA_ENUM_SIGNED);
    check_equal(signed64_value->enum_bits_ops->domain->bits, 64u);
    check_equal(cmeta_data_enum_assign_bits(signed8_value, &signed8_storage,
                                            UINT64_C(128)), CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(signed8_value, &signed8_storage, &value),
                CMETA_OK);
    check_equal(value, UINT64_C(128));
    check_equal(signed8_storage, INT8_MIN);
    check_equal(cmeta_data_enum_assign_bits(unsigned8_value, &unsigned8_storage,
                                       UINT8_MAX), CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(unsigned8_value, &unsigned8_storage, &value),
                CMETA_OK);
    check_equal(value, UINT8_MAX);
    check_equal(cmeta_data_enum_assign_bits(signed64_value, &signed64_storage,
                                            UINT64_C(1) << 63), CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(signed64_value, &signed64_storage, &value),
                CMETA_OK);
    check_equal(value, UINT64_C(1) << 63);
    check_equal(signed64_storage, INT64_MIN);
  }

  it("preserves UINT64_MAX through canonical bits and Binary wire bytes") {
    static const uint8_t expected_wire[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    DataBind *codec = NULL;
    const cmeta_data_desc *data = NULL;
    const cmeta_data_desc *enum_data;
    const cmeta_data_struct_shape *shape;
    WideEnumStorage_t object;
    WideEnumStorage_t decoded;
    uint64_t value = 0u;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(sizeof(WideDomain_t), sizeof(uint64_t));
    check(WideDomain_Maximum == UINT64_MAX);
    check_equal(WideEnumStorage_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (!data || !data->shape) return;
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_equal(shape->field_count, 1u);
    if (shape->field_count != 1u || !shape->fields) return;
    enum_data = shape->fields[0].value;
    check_not_null(enum_data);
    if (!enum_data) return;
    check_equal(enum_data->kind, CMETA_DATA_ENUM);
    check_equal(enum_data->storage_type->size, sizeof(uint64_t));
    check_not_null(cmeta_data_enum_bits_ops_of(enum_data));

    WideEnumStorage_init(&object);
    WideEnumStorage_init(&decoded);
    check_equal(cmeta_data_enum_assign_bits(
                    enum_data, &object.value, UINT64_MAX), CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(
                    enum_data, &object.value, &value), CMETA_OK);
    check(value == UINT64_MAX);
    check(object.value == UINT64_MAX);

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(WideEnumStorage_to_bin(
                      codec, &object, &wire, &wire_len, &error),
                  DATA_BIND_OK);
      check_not_null(wire);
      check_equal(wire_len, sizeof(expected_wire));
      if (wire != NULL && wire_len == sizeof(expected_wire)) {
        check_equal(wire, expected_wire, sizeof(expected_wire));
        check_equal(WideEnumStorage_from_bin(
            codec, &decoded, wire, wire_len, &error), DATA_BIND_OK);
        check(decoded.value == UINT64_MAX);
      }
    }
    data_bind_binary_free(wire);
    WideEnumStorage_clear(&decoded);
    WideEnumStorage_clear(&object);
    data_bind_free(codec);
  }

  it("rejects an unknown enum through canonical bits and preserves the object") {
    static const char json[] = "{\"value\":42}";
    const cmeta_data_desc *data = NULL;
    const cmeta_data_desc *enum_data;
    const cmeta_data_struct_shape *shape;
    DataBind *codec = NULL;
    Signed8Domain_t candidate = 0;
    Signed8Storage_t object = {.value = Signed8Domain_Maximum};
    Signed8Storage_t before = object;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(Signed8Storage_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (!data || !data->shape) return;
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_equal(shape->field_count, 1u);
    if (shape->field_count != 1u || !shape->fields) return;
    enum_data = shape->fields[0].value;
    check_not_null(enum_data);
    if (!enum_data) return;
    check_equal(cmeta_data_enum_assign_bits(
                    enum_data, &candidate, UINT64_C(42)),
                CMETA_INVALID_ARGUMENT);
    check_equal(candidate, 0);

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(Signed8Storage_from_json(
                    codec, &object, json, sizeof(json) - 1u, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(object.value, before.value);
    check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
    data_bind_free(codec);
  }

  it("accepts a flags mask and rejects invalid bits without mutation") {
    static const char invalid_json[] = "{\"value\":4}";
    const cmeta_data_desc *data = NULL;
    const cmeta_data_desc *enum_data;
    const cmeta_data_struct_shape *shape;
    DataBind *codec = NULL;
    FlagStorage_t object = {0};
    FlagStorage_t before;
    uint64_t bits = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(FlagStorage_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (!data || !data->shape) return;
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_equal(shape->field_count, 1u);
    if (shape->field_count != 1u || !shape->fields) return;
    enum_data = shape->fields[0].value;
    check_not_null(enum_data);
    if (!enum_data) return;
    check_not_null(cmeta_data_enum_bits_ops_of(enum_data));
    check_equal(cmeta_data_enum_assign_bits(
                    enum_data, &object.value,
                    UINT64_C(1) | UINT64_C(2)),
                CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(
                    enum_data, &object.value, &bits), CMETA_OK);
    check_equal(bits, UINT64_C(3));
    check_equal(object.value, Permission_Read | Permission_Write);
    check_equal(cmeta_data_enum_bits_restore_zero(
                    enum_data, &object.value), CMETA_OK);
    before = object;
    check_equal(cmeta_data_enum_assign_bits(
                    enum_data, &object.value, UINT64_C(4)),
                CMETA_INVALID_ARGUMENT);
    check_equal(object.value, before.value);

    check_equal(cmeta_data_enum_assign_bits(
                    enum_data, &object.value,
                    UINT64_C(1) | UINT64_C(2)),
                CMETA_OK);
    before = object;
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(FlagStorage_from_json(
                    codec, &object, invalid_json,
                    sizeof(invalid_json) - 1u, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(object.value, before.value);
    data_bind_free(codec);
  }

  it("publishes provider-backed UUID structural metadata without a descriptor") {
    const cmeta_data_desc *data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(UuidStorage_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (data) {
      const cmeta_data_struct_shape *shape = data->shape;
      const cmeta_data_desc *uuid = shape->fields[0].value;
      cmeta_type_desc copied = cmeta_uuid_cmeta_type;
      cmeta_type_identity identity = *copied.identity;
      copied.identity = &identity;
      check(cmeta_uuid_cmeta_data_valid(uuid));
      check_equal(uuid->kind, CMETA_DATA_STRING);
      check(cmeta_type_equal(uuid->storage_type, &copied));
      check_equal(uuid->storage_type->size, sizeof(((UuidStorage_t *)0)->value));
    }
  }
}

#include "data_bind_internal.h"
#include "data_bind_cmeta.h"
#include "tinytest.h"

#include <string.h>

typedef struct reentrant_cancel_context {
  data_bind_stream_t *stream;
  size_t calls;
  DataBindStatus cancel_status;
} reentrant_cancel_context_t;

static const char DATA_BIND_SET_INT_SCHEMA[] =
    "message NumberSet { set<int32> ids; }";
static const char DATA_BIND_SET_INT_DUPLICATES_JSON[] =
    "{\"ids\":[3,1,3,2,1]}";
static const char DATA_BIND_SET_STRING_SCHEMA[] =
    "message StringSet { set<string> tags; }";
static const char DATA_BIND_SET_STRING_DUPLICATES_JSON[] =
    "{\"tags\":[\"alpha\",\"beta\",\"alpha\",\"beta\"]}";
static const char DATA_BIND_MAP_SCHEMA[] =
    "message Attributes { map<string,int32> attrs; }";
static const char DATA_BIND_MAP_JSON[] =
    "{\"attrs\":{\"z\":1,\"a\":2}}";
static const char DATA_BIND_RANGE_SCHEMA[] =
    "message RangeOwner { int32 id; list<int32> values; set<int32> ids; "
    "map<string,int32> attrs; }";
static const char DATA_BIND_RANGE_JSON[] =
    "{\"id\":7,\"values\":[10,20],\"ids\":[3,1],\"attrs\":{\"z\":1,\"a\":2}}";

static DataBindRecordAction cancel_stream_from_callback(void *user_data,
                                                        const DataBindValue *record,
                                                        uint64_t record_index) {
  reentrant_cancel_context_t *context = (reentrant_cancel_context_t *)user_data;
  if (context == NULL || context->stream == NULL || record == NULL || record_index != 0u)
    return DATA_BIND_RECORD_ERROR;
  context->calls++;
  context->cancel_status = data_bind_stream_cancel(context->stream);
  return DATA_BIND_RECORD_CANCEL;
}

spec("data_bind dynamic CSTL storage") {
  it("versions every borrowed container range and rejects traversal after mutation") {
    const cmeta_range_flags ordered_flags =
        CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *root = NULL;
    const DataBindValue *values = NULL;
    const DataBindValue *ids = NULL;
    const DataBindValue *attrs = NULL;
    cmeta_range range = {0};
    cmeta_range_cursor cursor = {0};
    DataBindValueRef value_ref = {0};
    DataBindFieldRef field_ref = {0};
    DataBindMapEntryRef entry_ref = {0};

    check_equal(data_bind_create_from_text(DATA_BIND_RANGE_SCHEMA,
                                           strlen(DATA_BIND_RANGE_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_parse_json(codec, "RangeOwner", DATA_BIND_RANGE_JSON,
                                       strlen(DATA_BIND_RANGE_JSON), &root, &error),
                  DATA_BIND_OK);
    check_not_null(root);
    if (root != NULL) {
      values = data_bind_value_get(root, "values");
      ids = data_bind_value_get(root, "ids");
      attrs = data_bind_value_get(root, "attrs");
    }
    check_not_null(values);
    check_not_null(ids);
    check_not_null(attrs);

    if (root != NULL) {
      check_equal(data_bind_cmeta_range_init(root, DATA_BIND_CMETA_RANGE_FIELDS,
                                             &range),
                  DATA_BIND_OK);
      check_equal(range.flags, ordered_flags);
      check_not_null(range.current_version);
      check(range.version != UINT64_C(0));
      if (range.current_version != NULL)
        check_equal(range.version, range.current_version(range.object));
      check_equal(data_bind_internal_test_touch_generation(root), DATA_BIND_OK);
      check_equal(cmeta_range_next(&range, &cursor, &field_ref), CMETA_GEN_MUTATED);
      check_equal(cursor.index, (size_t)0u);
      check_null(field_ref.value);
    }

    if (values != NULL) {
      memset(&range, 0, sizeof(range));
      memset(&cursor, 0, sizeof(cursor));
      check_equal(data_bind_cmeta_range_init(values, DATA_BIND_CMETA_RANGE_VALUES,
                                             &range),
                  DATA_BIND_OK);
      check_equal(range.flags, ordered_flags);
      check_not_null(range.current_version);
      check(range.version != UINT64_C(0));
      if (range.current_version != NULL)
        check_equal(range.version, range.current_version(range.object));
      check_equal(data_bind_internal_test_touch_generation((DataBindValue *)values),
                  DATA_BIND_OK);
      check_equal(cmeta_range_next(&range, &cursor, &value_ref), CMETA_GEN_MUTATED);
      check_equal(cursor.index, (size_t)0u);
      check_null(value_ref.value);
    }

    if (ids != NULL) {
      memset(&range, 0, sizeof(range));
      memset(&cursor, 0, sizeof(cursor));
      check_equal(data_bind_cmeta_range_init(ids, DATA_BIND_CMETA_RANGE_VALUES,
                                             &range),
                  DATA_BIND_OK);
      check_equal(range.flags, ordered_flags | CMETA_RANGE_UNIQUE);
      check_not_null(range.current_version);
      check(range.version != UINT64_C(0));
      if (range.current_version != NULL)
        check_equal(range.version, range.current_version(range.object));
      check_equal(data_bind_internal_test_touch_generation((DataBindValue *)ids),
                  DATA_BIND_OK);
      check_equal(cmeta_range_next(&range, &cursor, &value_ref), CMETA_GEN_MUTATED);
      check_equal(cursor.index, (size_t)0u);
      check_null(value_ref.value);
    }

    if (attrs != NULL) {
      memset(&range, 0, sizeof(range));
      memset(&cursor, 0, sizeof(cursor));
      check_equal(data_bind_cmeta_range_init(attrs, DATA_BIND_CMETA_RANGE_MAP_ENTRIES,
                                             &range),
                  DATA_BIND_OK);
      check_equal(range.flags, ordered_flags);
      check_not_null(range.current_version);
      check(range.version != UINT64_C(0));
      if (range.current_version != NULL)
        check_equal(range.version, range.current_version(range.object));
      check_equal(data_bind_internal_test_touch_generation((DataBindValue *)attrs),
                  DATA_BIND_OK);
      check_equal(cmeta_range_next(&range, &cursor, &entry_ref), CMETA_GEN_MUTATED);
      check_equal(cursor.index, (size_t)0u);
      check_null(entry_ref.value);
    }

    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("preserves JSON map insertion order with ordered Map storage") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *root = NULL;
    const DataBindValue *attrs = NULL;
    DataBindMapEntry first = {0};
    DataBindMapEntry second = {0};

    check_equal(data_bind_create_from_text(DATA_BIND_MAP_SCHEMA,
                                           strlen(DATA_BIND_MAP_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_parse_json(codec, "Attributes", DATA_BIND_MAP_JSON,
                                       strlen(DATA_BIND_MAP_JSON), &root, &error),
                  DATA_BIND_OK);

    if (root != NULL) attrs = data_bind_value_get(root, "attrs");
    check_not_null(attrs);
    if (attrs != NULL) {
      check_equal(data_bind_internal_storage_kind(attrs),
                  DB_INTERNAL_STORAGE_ORDERED_MAP);
      check_equal(data_bind_value_count(attrs), (size_t)2u);
      first = data_bind_value_map_entry_at(attrs, 0u);
      second = data_bind_value_map_entry_at(attrs, 1u);
      check_equal(first.key, "z");
      check_equal(data_bind_value_as_int(first.value), 1);
      check_equal(second.key, "a");
      check_equal(data_bind_value_as_int(second.value), 2);
    }

    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("replaces a duplicate binary map key without moving its first position") {
    static const uint8_t wire[] = {
        3, 0, 0, 0,
        1, 0, 0, 0, 'z', 1, 0, 0, 0,
        1, 0, 0, 0, 'a', 2, 0, 0, 0,
        1, 0, 0, 0, 'z', 9, 0, 0, 0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindObject *object = NULL;
    const DataBindValue *attrs = NULL;
    DataBindMapEntry first = {0};
    DataBindMapEntry second = {0};

    check_equal(data_bind_create_from_text(DATA_BIND_MAP_SCHEMA,
                                           strlen(DATA_BIND_MAP_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_object_from_bin(codec, "Attributes", wire,
                                            sizeof(wire), &object, &error),
                  DATA_BIND_OK);

    if (object != NULL)
      attrs = data_bind_value_get(data_bind_object_value(object), "attrs");
    check_not_null(attrs);
    if (attrs != NULL) {
      check_equal(data_bind_value_count(attrs), (size_t)2u);
      first = data_bind_value_map_entry_at(attrs, 0u);
      second = data_bind_value_map_entry_at(attrs, 1u);
      check_equal(first.key, "z");
      check_equal(data_bind_value_as_int(first.value), 9);
      check_equal(second.key, "a");
      check_equal(data_bind_value_as_int(second.value), 2);
      check_equal(data_bind_internal_storage_kind(attrs),
                  DB_INTERNAL_STORAGE_ORDERED_MAP);
    }

    data_bind_object_free(object);
    data_bind_free(codec);
  }

  it("keeps a cloned Map ordered and owned after releasing its source") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *source = NULL;
    DataBindValue *clone = NULL;
    const DataBindValue *attrs = NULL;
    DataBindMapEntry first = {0};
    DataBindMapEntry second = {0};

    check_equal(data_bind_create_from_text(DATA_BIND_MAP_SCHEMA,
                                           strlen(DATA_BIND_MAP_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_parse_json(codec, "Attributes", DATA_BIND_MAP_JSON,
                                       strlen(DATA_BIND_MAP_JSON), &source, &error),
                  DATA_BIND_OK);
    if (source != NULL)
      check_equal(data_bind_value_clone(source, &clone), DATA_BIND_OK);
    check_not_null(clone);

    data_bind_value_free(source);
    source = NULL;

    if (clone != NULL) attrs = data_bind_value_get(clone, "attrs");
    check_not_null(attrs);
    if (attrs != NULL) {
      check_equal(data_bind_internal_storage_kind(attrs),
                  DB_INTERNAL_STORAGE_ORDERED_MAP);
      first = data_bind_value_map_entry_at(attrs, 0u);
      second = data_bind_value_map_entry_at(attrs, 1u);
      check_equal(first.key, "z");
      check_equal(data_bind_value_as_int(first.value), 1);
      check_equal(second.key, "a");
      check_equal(data_bind_value_as_int(second.value), 2);
    }

    data_bind_value_free(clone);
    data_bind_free(codec);
  }

  it("round-trips JSON and binary maps in insertion order") {
    static const char expected_json[] = "{\"attrs\":{\"z\":1,\"a\":2}}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindObject *source = NULL;
    DataBindObject *roundtrip = NULL;
    char *json = NULL;
    size_t json_len = 0u;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;
    const DataBindValue *attrs = NULL;
    DataBindMapEntry first = {0};
    DataBindMapEntry second = {0};

    check_equal(data_bind_create_from_text(DATA_BIND_MAP_SCHEMA,
                                           strlen(DATA_BIND_MAP_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_object_from_json(codec, "Attributes",
                                             DATA_BIND_MAP_JSON,
                                             strlen(DATA_BIND_MAP_JSON),
                                             &source, &error),
                  DATA_BIND_OK);
    if (source != NULL)
      check_equal(data_bind_object_serialize_json(codec, source, &json,
                                                  &json_len, &error),
                  DATA_BIND_OK);
    check_not_null(json);
    if (json != NULL) {
      check_equal(json_len, strlen(expected_json));
      check_equal(json, expected_json);
    }
    if (source != NULL)
      check_equal(data_bind_object_serialize_bin(codec, source, &wire,
                                                 &wire_len, &error),
                  DATA_BIND_OK);
    if (wire != NULL)
      check_equal(data_bind_object_from_bin(codec, "Attributes", wire,
                                            wire_len, &roundtrip, &error),
                  DATA_BIND_OK);

    if (roundtrip != NULL)
      attrs = data_bind_value_get(data_bind_object_value(roundtrip), "attrs");
    check_not_null(attrs);
    if (attrs != NULL) {
      first = data_bind_value_map_entry_at(attrs, 0u);
      second = data_bind_value_map_entry_at(attrs, 1u);
      check_equal(first.key, "z");
      check_equal(data_bind_value_as_int(first.value), 1);
      check_equal(second.key, "a");
      check_equal(data_bind_value_as_int(second.value), 2);
    }

    data_bind_object_free(roundtrip);
    data_bind_binary_free(wire);
    data_bind_serialized_free(json);
    data_bind_object_free(source);
    data_bind_free(codec);
  }

  it("deduplicates int32 sets in first-insertion order with ordered Set storage") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *root = NULL;
    const DataBindValue *ids = NULL;

    check_equal(data_bind_create_from_text(DATA_BIND_SET_INT_SCHEMA,
                                           strlen(DATA_BIND_SET_INT_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_parse_json(codec, "NumberSet",
                                       DATA_BIND_SET_INT_DUPLICATES_JSON,
                                       strlen(DATA_BIND_SET_INT_DUPLICATES_JSON),
                                       &root, &error),
                  DATA_BIND_OK);

    check_not_null(root);
    if (root != NULL) ids = data_bind_value_get(root, "ids");
    check_not_null(ids);
    if (ids != NULL) {
      check_equal(data_bind_value_kind(ids), DATA_BIND_VALUE_SET);
      check_equal(data_bind_value_count(ids), (size_t)3u);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 0u)), 3);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 1u)), 1);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 2u)), 2);
      check_equal(data_bind_internal_storage_kind(ids),
                  DB_INTERNAL_STORAGE_ORDERED_SET);
    }

    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("deduplicates supported string sets without canonical element identity") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *root = NULL;
    const DataBindValue *tags = NULL;

    check_equal(data_bind_create_from_text(DATA_BIND_SET_STRING_SCHEMA,
                                           strlen(DATA_BIND_SET_STRING_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_parse_json(codec, "StringSet",
                                       DATA_BIND_SET_STRING_DUPLICATES_JSON,
                                       strlen(DATA_BIND_SET_STRING_DUPLICATES_JSON),
                                       &root, &error),
                  DATA_BIND_OK);

    check_not_null(root);
    if (root != NULL) tags = data_bind_value_get(root, "tags");
    check_not_null(tags);
    if (tags != NULL) {
      check_equal(data_bind_value_count(tags), (size_t)2u);
      check_equal(data_bind_value_as_string(data_bind_value_at(tags, 0u)),
                  "alpha");
      check_equal(data_bind_value_as_string(data_bind_value_at(tags, 1u)),
                  "beta");
    }

    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("keeps a cloned Set unique and ordered after releasing its source") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *source = NULL;
    DataBindValue *clone = NULL;
    const DataBindValue *ids = NULL;

    check_equal(data_bind_create_from_text(DATA_BIND_SET_INT_SCHEMA,
                                           strlen(DATA_BIND_SET_INT_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_parse_json(codec, "NumberSet",
                                       DATA_BIND_SET_INT_DUPLICATES_JSON,
                                       strlen(DATA_BIND_SET_INT_DUPLICATES_JSON),
                                       &source, &error),
                  DATA_BIND_OK);
    check_not_null(source);
    if (source != NULL)
      check_equal(data_bind_value_clone(source, &clone), DATA_BIND_OK);
    check_not_null(clone);

    data_bind_value_free(source);
    source = NULL;

    if (clone != NULL) ids = data_bind_value_get(clone, "ids");
    check_not_null(ids);
    if (ids != NULL) {
      check_equal(data_bind_internal_storage_kind(ids),
                  DB_INTERNAL_STORAGE_ORDERED_SET);
      check_equal(data_bind_value_count(ids), (size_t)3u);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 0u)), 3);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 1u)), 1);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 2u)), 2);
    }

    data_bind_value_free(clone);
    data_bind_free(codec);
  }

  it("round-trips JSON Set values in unique first-insertion order") {
    static const char expected_json[] = "{\"ids\":[3,1,2]}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindObject *source = NULL;
    DataBindObject *roundtrip = NULL;
    char *serialized = NULL;
    size_t serialized_len = 0u;
    const DataBindValue *ids = NULL;

    check_equal(data_bind_create_from_text(DATA_BIND_SET_INT_SCHEMA,
                                           strlen(DATA_BIND_SET_INT_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_object_from_json(codec, "NumberSet",
                                             DATA_BIND_SET_INT_DUPLICATES_JSON,
                                             strlen(DATA_BIND_SET_INT_DUPLICATES_JSON),
                                             &source, &error),
                  DATA_BIND_OK);
    check_not_null(source);
    if (source != NULL)
      check_equal(data_bind_object_serialize_json(codec, source, &serialized,
                                                  &serialized_len, &error),
                  DATA_BIND_OK);
    check_not_null(serialized);
    if (serialized != NULL) {
      check_equal(serialized_len, strlen(expected_json));
      check_equal(serialized, expected_json);
      check_equal(data_bind_object_from_json(codec, "NumberSet", serialized,
                                             serialized_len, &roundtrip, &error),
                  DATA_BIND_OK);
    }

    if (roundtrip != NULL)
      ids = data_bind_value_get(data_bind_object_value(roundtrip), "ids");
    check_not_null(ids);
    if (ids != NULL) {
      check_equal(data_bind_value_count(ids), (size_t)3u);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 0u)), 3);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 1u)), 1);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 2u)), 2);
    }

    data_bind_object_free(roundtrip);
    data_bind_serialized_free(serialized);
    data_bind_object_free(source);
    data_bind_free(codec);
  }

  it("round-trips binary Set values in unique first-insertion order") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindObject *source = NULL;
    DataBindObject *roundtrip = NULL;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;
    const DataBindValue *ids = NULL;

    check_equal(data_bind_create_from_text(DATA_BIND_SET_INT_SCHEMA,
                                           strlen(DATA_BIND_SET_INT_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_object_from_json(codec, "NumberSet",
                                             DATA_BIND_SET_INT_DUPLICATES_JSON,
                                             strlen(DATA_BIND_SET_INT_DUPLICATES_JSON),
                                             &source, &error),
                  DATA_BIND_OK);
    check_not_null(source);
    if (source != NULL)
      check_equal(data_bind_object_serialize_bin(codec, source, &wire, &wire_len,
                                                 &error),
                  DATA_BIND_OK);
    check_not_null(wire);
    if (wire != NULL)
      check_equal(data_bind_object_from_bin(codec, "NumberSet", wire, wire_len,
                                            &roundtrip, &error),
                  DATA_BIND_OK);

    if (roundtrip != NULL)
      ids = data_bind_value_get(data_bind_object_value(roundtrip), "ids");
    check_not_null(ids);
    if (ids != NULL) {
      check_equal(data_bind_value_count(ids), (size_t)3u);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 0u)), 3);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 1u)), 1);
      check_equal(data_bind_value_as_int(data_bind_value_at(ids, 2u)), 2);
    }

    data_bind_object_free(roundtrip);
    data_bind_binary_free(wire);
    data_bind_object_free(source);
    data_bind_free(codec);
  }

  it("marks Set value ranges as unique and ordered") {
    static const char json[] = "{\"ids\":[3,1,2]}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *root = NULL;
    const DataBindValue *ids = NULL;
    cmeta_range range = {0};
    cmeta_range_cursor cursor = {0};
    DataBindValueRef ref = {0};

    check_equal(data_bind_create_from_text(DATA_BIND_SET_INT_SCHEMA,
                                           strlen(DATA_BIND_SET_INT_SCHEMA),
                                           &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      check_equal(data_bind_parse_json(codec, "NumberSet", json, strlen(json),
                                       &root, &error),
                  DATA_BIND_OK);
    check_not_null(root);
    if (root != NULL) ids = data_bind_value_get(root, "ids");
    check_not_null(ids);
    if (ids != NULL)
      check_equal(data_bind_cmeta_range_init(ids, DATA_BIND_CMETA_RANGE_VALUES,
                                             &range),
                  DATA_BIND_OK);

    check((range.flags & CMETA_RANGE_UNIQUE) != 0u);
    check((range.flags & CMETA_RANGE_ORDERED) != 0u);
    check_equal(cmeta_range_size(&range), (size_t)3u);
    check_equal(cmeta_range_next(&range, &cursor, &ref), CMETA_GEN_VALUE);
    check_equal(data_bind_value_as_int(ref.value), 3);
    check_equal(cmeta_range_next(&range, &cursor, &ref), CMETA_GEN_VALUE);
    check_equal(data_bind_value_as_int(ref.value), 1);
    check_equal(cmeta_range_next(&range, &cursor, &ref),
                CMETA_GEN_VALUE_AND_DONE);
    check_equal(data_bind_value_as_int(ref.value), 2);

    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("preserves ordered object fields and list values in Vec storage") {
    static const char schema[] =
        "message Ordered { int32 first; int32 second; int32 third; } "
        "message Values { list<int32> values; }";
    static const char object_json[] = "{\"first\":1,\"second\":2,\"third\":3}";
    static const char list_json[] = "{\"values\":[10,20,30]}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *object = NULL;
    DataBindValue *holder = NULL;
    const DataBindValue *list;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(data_bind_parse_json(codec, "Ordered", object_json,
                                       strlen(object_json), &object, &error),
                  DATA_BIND_OK);
      check_equal(data_bind_parse_json(codec, "Values", list_json,
                                       strlen(list_json), &holder, &error),
                  DATA_BIND_OK);
    }

    check_not_null(object);
    check_not_null(holder);
    if (object != NULL) {
      check_equal(data_bind_internal_storage_kind(object), DB_INTERNAL_STORAGE_VEC);
      check_equal(data_bind_value_field_count(object), (size_t)3u);
      check_equal(data_bind_value_field_name(object, 0u), "first");
      check_equal(data_bind_value_field_name(object, 1u), "second");
      check_equal(data_bind_value_field_name(object, 2u), "third");
    }

    list = data_bind_value_get(holder, "values");
    check_not_null(list);
    if (list != NULL) {
      check_equal(data_bind_internal_storage_kind(list), DB_INTERNAL_STORAGE_VEC);
      check_equal(data_bind_value_count(list), (size_t)3u);
      check_equal(data_bind_value_as_int(data_bind_value_at(list, 0u)), 10);
      check_equal(data_bind_value_as_int(data_bind_value_at(list, 1u)), 20);
      check_equal(data_bind_value_as_int(data_bind_value_at(list, 2u)), 30);
    }

    data_bind_value_free(holder);
    data_bind_value_free(object);
    data_bind_free(codec);
  }

  it("keeps cloned object and list storage independent of the source") {
    static const char schema[] =
        "message Envelope { int32 first; int32 second; int32 third; "
        "list<int32> values; }";
    static const char json[] =
        "{\"first\":1,\"second\":2,\"third\":3,\"values\":[10,20,30]}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *source = NULL;
    DataBindValue *clone = NULL;
    const DataBindValue *list;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(data_bind_parse_json(codec, "Envelope", json, strlen(json),
                                       &source, &error), DATA_BIND_OK);
    }
    check_not_null(source);
    if (source != NULL)
      check_equal(data_bind_value_clone(source, &clone), DATA_BIND_OK);
    check_not_null(clone);

    data_bind_value_free(source);
    source = NULL;

    if (clone != NULL) {
      check_equal(data_bind_internal_storage_kind(clone), DB_INTERNAL_STORAGE_VEC);
      check_equal(data_bind_value_field_name(clone, 0u), "first");
      check_equal(data_bind_value_field_name(clone, 1u), "second");
      check_equal(data_bind_value_field_name(clone, 2u), "third");
      list = data_bind_value_get(clone, "values");
      check_not_null(list);
      if (list != NULL) {
        check_equal(data_bind_internal_storage_kind(list), DB_INTERNAL_STORAGE_VEC);
        check_equal(data_bind_value_count(list), (size_t)3u);
        check_equal(data_bind_value_as_int(data_bind_value_at(list, 0u)), 10);
        check_equal(data_bind_value_as_int(data_bind_value_at(list, 1u)), 20);
        check_equal(data_bind_value_as_int(data_bind_value_at(list, 2u)), 30);
      }
    }

    data_bind_value_free(clone);
    data_bind_free(codec);
  }

  it("reports retained stream Vec capacity exhaustion as a limit") {
    static const char schema[] = "message Item { int32 id; }";
    static const char json[] = "[{\"id\":1},{\"id\":2}]";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStreamLimits limits = DATA_BIND_STREAM_LIMITS_INIT;
    DataBind *codec = NULL;
    DataBindValue *result = NULL;
    data_bind_stream_t *stream = NULL;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      stream = data_bind_stream_json_all_create(codec, "Item", &result, &error);
    check_not_null(stream);
    if (stream != NULL) {
      limits.max_result_count = 1u;
      check_equal(data_bind_stream_set_limits(stream, &limits), DATA_BIND_OK);
      check_equal(data_bind_stream_feed(stream, json, strlen(json)),
                  DATA_BIND_ERR_LIMIT);
      check_equal(error.code, DATA_BIND_ERR_LIMIT);
      check_null(result);
    }

    data_bind_stream_destroy(stream);
    data_bind_value_free(result);
    data_bind_free(codec);
  }

  it("allows a retained JSON callback to cancel its stream reentrantly") {
    static const char schema[] = "message Item { int32 id; }";
    static const char json[] = "[{\"id\":1},{\"id\":2}]";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *result = NULL;
    data_bind_stream_t *stream = NULL;
    reentrant_cancel_context_t context = {0};

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      stream = data_bind_stream_json_all_create(codec, "Item", &result, &error);
    check_not_null(stream);
    if (stream != NULL) {
      context.stream = stream;
      check_equal(data_bind_stream_set_record_callback(
                      stream, cancel_stream_from_callback, &context),
                  DATA_BIND_OK);
      check_equal(data_bind_stream_feed(stream, json, strlen(json)),
                  DATA_BIND_ERR_CANCELED);
      check_equal(error.code, DATA_BIND_ERR_CANCELED);
      check_equal(context.calls, (size_t)1u);
      check_equal(context.cancel_status, DATA_BIND_ERR_CANCELED);
      check_null(result);
      check_equal(data_bind_stream_finish(stream), DATA_BIND_ERR_CANCELED);
    }

    data_bind_stream_destroy(stream);
    data_bind_value_free(result);
    data_bind_free(codec);
  }

  it("allows a retained CSV callback to cancel its stream reentrantly") {
    static const char schema[] = "message Item { int32 id; }";
    static const char csv[] = "id\n1\n2\n";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *result = NULL;
    data_bind_stream_t *stream = NULL;
    reentrant_cancel_context_t context = {0};

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    if (codec != NULL)
      stream = data_bind_stream_csv_all_create(codec, "Item", &result, &error);
    check_not_null(stream);
    if (stream != NULL) {
      context.stream = stream;
      check_equal(data_bind_stream_set_record_callback(
                      stream, cancel_stream_from_callback, &context),
                  DATA_BIND_OK);
      check_equal(data_bind_stream_feed(stream, csv, strlen(csv)),
                  DATA_BIND_ERR_CANCELED);
      check_equal(error.code, DATA_BIND_ERR_CANCELED);
      check_equal(context.calls, (size_t)1u);
      check_equal(context.cancel_status, DATA_BIND_ERR_CANCELED);
      check_null(result);
      check_equal(data_bind_stream_finish(stream), DATA_BIND_ERR_CANCELED);
    }

    data_bind_stream_destroy(stream);
    data_bind_value_free(result);
    data_bind_free(codec);
  }
}

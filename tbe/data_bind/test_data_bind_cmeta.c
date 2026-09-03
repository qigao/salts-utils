#include "data_bind_cmeta.h"
#include "tinytest.h"

#include <string.h>

static DataBindValue *parse_fixture(DataBind **out_codec) {
  static const char schema[] =
      "message Book { list<uint32> values; set<string> tags; map<string,int32> attrs; }";
  static const char json[] =
      "{\"values\":[3,4],\"tags\":[\"alpha\",\"beta\"],\"attrs\":{\"x\":30,\"y\":40}}";
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindValue *root = NULL;

  *out_codec = NULL;
  check_equal(data_bind_create_from_text(schema, strlen(schema), out_codec, &error), DATA_BIND_OK);
  if (*out_codec != NULL) {
    check_equal(data_bind_parse_json(*out_codec, "Book", json, strlen(json), &root, &error),
                DATA_BIND_OK);
  }
  return root;
}

spec("data_bind CMeta adapter") {
  it("should expose ordered object fields as borrowed references") {
    static const char *const names[] = {"values", "tags", "attrs"};
    DataBind *codec = NULL;
    DataBindValue *root = parse_fixture(&codec);
    cmeta_range range = {0};
    cmeta_range_cursor cursor = {0};
    DataBindFieldRef ref = {0};
    size_t i;

    check_not_null(root);
    check_equal(data_bind_cmeta_range_init(root, DATA_BIND_CMETA_RANGE_FIELDS, &range),
                DATA_BIND_OK);
    check(range.element_type == data_bind_cmeta_field_ref_type());
    check((range.flags & (CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE)) ==
          (CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE));
    check_equal(cmeta_range_size(&range), 3u);

    for (i = 0; i < 3u; ++i) {
      check_equal(cmeta_range_next(&range, &cursor, &ref),
                  i == 2u ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE);
      check(strcmp(ref.name, names[i]) == 0);
      check(ref.value == data_bind_value_field_at(root, i));
    }
    check_equal(cmeta_range_next(&range, &cursor, &ref), CMETA_GEN_DONE);

    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("should expose list set and map values without buffering") {
    DataBind *codec = NULL;
    DataBindValue *root = parse_fixture(&codec);
    const DataBindValue *values = data_bind_value_get(root, "values");
    const DataBindValue *tags = data_bind_value_get(root, "tags");
    const DataBindValue *attrs = data_bind_value_get(root, "attrs");
    cmeta_range range = {0};
    cmeta_range_cursor cursor = {0};
    DataBindValueRef value_ref = {0};
    DataBindMapEntryRef entry_ref = {0};

    check_equal(data_bind_cmeta_range_init(values, DATA_BIND_CMETA_RANGE_VALUES, &range),
                DATA_BIND_OK);
    check(range.element_type == data_bind_cmeta_value_ref_type());
    check_equal(cmeta_range_size(&range), 2u);
    check_equal(cmeta_range_next(&range, &cursor, &value_ref), CMETA_GEN_VALUE);
    check_equal(data_bind_value_as_int(value_ref.value), 3);
    check_equal(cmeta_range_next(&range, &cursor, &value_ref), CMETA_GEN_VALUE_AND_DONE);
    check_equal(data_bind_value_as_int(value_ref.value), 4);

    cursor = (cmeta_range_cursor){0};
    check_equal(data_bind_cmeta_range_init(tags, DATA_BIND_CMETA_RANGE_VALUES, &range),
                DATA_BIND_OK);
    check_equal(cmeta_range_next(&range, &cursor, &value_ref), CMETA_GEN_VALUE);
    check(strcmp(data_bind_value_as_string(value_ref.value), "alpha") == 0);

    cursor = (cmeta_range_cursor){0};
    check_equal(data_bind_cmeta_range_init(attrs, DATA_BIND_CMETA_RANGE_MAP_ENTRIES, &range),
                DATA_BIND_OK);
    check(range.element_type == data_bind_cmeta_map_entry_ref_type());
    check_equal(cmeta_range_next(&range, &cursor, &entry_ref), CMETA_GEN_VALUE);
    check(strcmp(entry_ref.key, "x") == 0);
    check_equal(data_bind_value_as_int(entry_ref.value), 30);
    check_equal(cmeta_range_next(&range, &cursor, &entry_ref), CMETA_GEN_VALUE_AND_DONE);
    check(strcmp(entry_ref.key, "y") == 0);
    check_equal(data_bind_value_as_int(entry_ref.value), 40);

    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("should fail fast on kind mismatch and leave the range empty") {
    DataBind *codec = NULL;
    DataBindValue *root = parse_fixture(&codec);
    const DataBindValue *values = data_bind_value_get(root, "values");
    cmeta_range range = {0};

    range.object = root;
    range.element_type = data_bind_cmeta_value_ref_type();
    check_equal(data_bind_cmeta_range_init(values, DATA_BIND_CMETA_RANGE_FIELDS, &range),
                DATA_BIND_ERR_INVALID_ARG);
    check_null(range.object);
    check_null(range.element_type);
    check_null(range.size);
    check_null(range.next);

    check_not_null(data_bind_cmeta_value_ref_type()->identity);
    check(strcmp(data_bind_cmeta_value_ref_type()->identity->stable_atom_id,
                 "turbo_parser.data_bind.ValueRef.v1") == 0);

    data_bind_value_free(root);
    data_bind_free(codec);
  }
}

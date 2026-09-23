#include "data_bind_cmeta.h"
#include "schema_cmeta.h"
#include "tinytest.h"

#include <cmeta/data.h>
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
  it("should publish stable dynamic CMeta identity across codecs") {
    DataBind *left_codec = NULL;
    DataBind *right_codec = NULL;
    DataBindValue *left = parse_fixture(&left_codec);
    DataBindValue *right = parse_fixture(&right_codec);
    const cmeta_type_identity *left_id;
    const cmeta_type_identity *right_id;

    check_not_null(left);
    check_not_null(right);
    left_id = data_bind_value_type_identity(left);
    right_id = data_bind_value_type_identity(right);
    check_not_null(left_id);
    check_not_null(right_id);
    check(cmeta_type_identity_equal(left_id, right_id));

    data_bind_value_free(right);
    data_bind_value_free(left);
    data_bind_free(right_codec);
    data_bind_free(left_codec);
  }

  it("should keep dynamic CMeta identity alive after codec destruction") {
    DataBind *codec = NULL;
    DataBindValue *root = parse_fixture(&codec);
    const cmeta_type_identity *identity;

    check_not_null(root);
    identity = data_bind_value_type_identity(root);
    check_not_null(identity);

    data_bind_free(codec);
    codec = NULL;
    check_not_null(data_bind_value_type_identity(root));
    check(cmeta_type_identity_equal(identity, data_bind_value_type_identity(root)));

    data_bind_value_free(root);
  }

  it("should reuse canonical int32 provider identity for scalar children") {
    DataBind *codec = NULL;
    DataBindValue *root = parse_fixture(&codec);
    DataBindValue *copy = NULL;
    const DataBindValue *attrs = data_bind_value_get(root, "attrs");
    const DataBindMapEntry x_entry = data_bind_value_map_entry_at(attrs, 0u);
    const DataBindValue *x = x_entry.value;
    const cmeta_data_desc *provider = schema_cmeta_builtin_data("int32");
    const cmeta_type_identity *provider_id = NULL;
    const cmeta_type_identity *value_id;

    check_not_null(root);
    check_not_null(attrs);
    check_not_null(x);
    check_not_null(provider);
    if (provider != NULL) {
      check_not_null(provider->storage_type);
      if (provider->storage_type != NULL) {
        provider_id = provider->storage_type->identity;
        check_not_null(provider_id);
      }
    }

    value_id = data_bind_value_type_identity(x);
    check_not_null(value_id);
    if (value_id != NULL && provider_id != NULL)
      check(cmeta_type_identity_equal(value_id, provider_id));

    check_equal(data_bind_value_clone(root, &copy), DATA_BIND_OK);
    check_not_null(copy);
    if (copy != NULL) {
      const DataBindValue *copy_attrs = data_bind_value_get(copy, "attrs");
      check_not_null(copy_attrs);
      if (copy_attrs != NULL) {
        const DataBindMapEntry copy_x_entry = data_bind_value_map_entry_at(copy_attrs, 0u);
        const cmeta_type_identity *copy_value_id =
            data_bind_value_type_identity(copy_x_entry.value);
        check_not_null(copy_value_id);
        if (copy_value_id != NULL && provider_id != NULL)
          check(cmeta_type_identity_equal(copy_value_id, provider_id));
      }
    }

    data_bind_value_free(copy);
    data_bind_value_free(root);
    data_bind_free(codec);
  }

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

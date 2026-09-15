#include "data_bind_internal.h"
#include "tinytest.h"

#include <string.h>

spec("data_bind dynamic CSTL storage") {
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
}

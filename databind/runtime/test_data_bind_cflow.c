#include "data_bind_cflow.h"
#include "tinytest.h"

#include <cflow/cflow.h>

#include <string.h>

typedef struct reactive_sink_state {
  size_t count;
  int values[2];
  int done;
  int failed;
} reactive_sink_state;

static bool collect_value_ref(void *user, const cmeta_type_desc *type, const void *value) {
  reactive_sink_state *state = (reactive_sink_state *)user;
  const DataBindValueRef *ref = (const DataBindValueRef *)value;
  if (state == NULL || type != data_bind_cmeta_value_ref_type() || ref == NULL ||
      ref->value == NULL || state->count >= 2u) {
    return false;
  }
  state->values[state->count++] = data_bind_value_as_int(ref->value);
  return true;
}

static void collect_error(void *user, const char *message) {
  reactive_sink_state *state = (reactive_sink_state *)user;
  (void)message;
  if (state != NULL) state->failed = 1;
}

static void collect_done(void *user) {
  reactive_sink_state *state = (reactive_sink_state *)user;
  if (state != NULL) state->done = 1;
}

static DataBindValue *parse_values(DataBind **out_codec) {
  static const char schema[] = "message Values { list<uint32> items; }";
  static const char json[] = "{\"items\":[7,9]}";
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindValue *root = NULL;

  *out_codec = NULL;
  check_equal(data_bind_create_from_text(schema, strlen(schema), out_codec, &error), DATA_BIND_OK);
  if (*out_codec != NULL) {
    check_equal(data_bind_parse_json(*out_codec, "Values", json, strlen(json), &root, &error),
                DATA_BIND_OK);
  }
  return root;
}

spec("data_bind CFlow adapter") {
  it("should create a reusable source-only CFlow stream") {
    DataBind *codec = NULL;
    DataBindValue *root = parse_values(&codec);
    const DataBindValue *items = data_bind_value_get(root, "items");
    cflow_stream stream = {0};
    const char *error = NULL;
    size_t count = 0u;

    check_equal(data_bind_cflow_stream_from_value(
                    items, DATA_BIND_CMETA_RANGE_VALUES, &stream),
                DATA_BIND_OK);
    check(cflow_stream_output_type(&stream) == data_bind_cmeta_value_ref_type());
    check(cflow_stream_count(&stream, &count, &error));
    check_equal(count, 2u);
    check_null(error);
    cflow_stream_destroy(&stream);

    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("should honor downstream demand and close without owning DataBind values") {
    DataBind *codec = NULL;
    DataBindValue *root = parse_values(&codec);
    const DataBindValue *items = data_bind_value_get(root, "items");
    cflow_graph surface = {0};
    cflow_graph normalized = {0};
    cflow_scheduler scheduler = {0};
    cflow_publisher source = {0};
    cflow_subscription subscription = {0};
    reactive_sink_state state = {0};
    cflow_subscriber_callbacks callbacks = {
        collect_value_ref, collect_error, collect_done, &state};
    cflow_subscriber sink = cflow_subscriber_from_callbacks(&callbacks);

    normalized.root = CMETA_INVALID_ID;
    cflow_graph_init(&surface, data_bind_cmeta_value_ref_type());
    check(cflow_graph_normalize(&normalized, &surface));
    check(cflow_scheduler_test_init(&scheduler));
    check_equal(data_bind_cflow_publisher_from_value(
                    items, DATA_BIND_CMETA_RANGE_VALUES, &source),
                DATA_BIND_OK);
    check(cflow_subscribe(&subscription, &normalized, &source, &scheduler, &sink));

    check(cflow_subscription_request(&subscription, 1u));
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    check_equal(state.count, 1u);
    check_equal(state.values[0], 7);
    check(!state.done);
    check(!state.failed);

    check(cflow_subscription_request(&subscription, 1u));
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    check_equal(state.count, 2u);
    check_equal(state.values[1], 9);
    check(state.done);
    check(cflow_subscription_is_done(&subscription));
    cflow_subscription_close(&subscription);

    check_equal(data_bind_value_count(items), 2u);
    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&normalized);
    cflow_graph_destroy(&surface);
    data_bind_value_free(root);
    data_bind_free(codec);
  }

  it("should cancel without consuming or releasing the owner") {
    DataBind *codec = NULL;
    DataBindValue *root = parse_values(&codec);
    const DataBindValue *items = data_bind_value_get(root, "items");
    cflow_graph surface = {0};
    cflow_graph normalized = {0};
    cflow_scheduler scheduler = {0};
    cflow_publisher source = {0};
    cflow_subscription subscription = {0};
    reactive_sink_state state = {0};
    cflow_subscriber_callbacks callbacks = {
        collect_value_ref, collect_error, collect_done, &state};
    cflow_subscriber sink = cflow_subscriber_from_callbacks(&callbacks);

    normalized.root = CMETA_INVALID_ID;
    cflow_graph_init(&surface, data_bind_cmeta_value_ref_type());
    check(cflow_graph_normalize(&normalized, &surface));
    check(cflow_scheduler_test_init(&scheduler));
    check_equal(data_bind_cflow_publisher_from_value(
                    items, DATA_BIND_CMETA_RANGE_VALUES, &source),
                DATA_BIND_OK);
    check(cflow_subscribe(&subscription, &normalized, &source, &scheduler, &sink));
    cflow_subscription_cancel(&subscription);
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    check(cflow_subscription_is_cancelled(&subscription));
    check_equal(state.count, 0u);
    cflow_subscription_close(&subscription);
    check_equal(data_bind_value_count(items), 2u);

    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&normalized);
    cflow_graph_destroy(&surface);
    data_bind_value_free(root);
    data_bind_free(codec);
  }
}

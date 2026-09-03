#include "typed_order.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "tinytest.h"
#include "salts_error.h"
#include "salts_lua_executor.h"
#include "salts_lua_worker.h"
#include "salts/thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ORDER_REQUEST_ID "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001"

typedef struct TestOrderApiContext {
  unsigned calls;
  unsigned async_calls;
  unsigned async_steps;
  unsigned async_destroys;
  unsigned cancellations;
  DataBindStatus next_status;
} TestOrderApiContext;

typedef struct TestAsyncOrderOperation {
  TestOrderApiContext *context;
  uint32_t id;
  tstr symbol;
  unsigned polls;
} TestAsyncOrderOperation;

typedef struct TestLuaClientNonOwnerContext {
  Orders_lua_client_t *client;
  const LuaOrder_t *request;
  OrderResult_t *response;
  DataBindError call_error;
  DataBindError close_error;
  DataBindStatus call_status;
  DataBindStatus close_status;
} TestLuaClientNonOwnerContext;

typedef struct TestLuaClientProducerContext {
  Orders_lua_client_t *client;
  const AsyncLuaOrder_t *request;
  Orders_lua_enrich_order_deferred_future_t *future;
  DataBindError error;
  DataBindStatus status;
} TestLuaClientProducerContext;

typedef struct TestManagedLuaClientContext {
  Orders_lua_client_t *client;
  Orders_lua_limits_t limits;
  DataBindError error;
  DataBindStatus start_status;
  DataBindStatus close_status;
} TestManagedLuaClientContext;

static void test_lua_client_non_owner(void *context) {
  TestLuaClientNonOwnerContext *test =
      (TestLuaClientNonOwnerContext *)context;
  test->call_status = Orders_lua_client_enrich_order_on_owner(
      test->client, test->request, test->response, &test->call_error);
  test->close_status =
      Orders_lua_client_close(test->client, &test->close_error);
}

static void test_lua_client_producer(void *context) {
  TestLuaClientProducerContext *test =
      (TestLuaClientProducerContext *)context;
  test->status = Orders_lua_client_enrich_order_deferred_async(
      test->client, test->request, &test->future, &test->error);
}

static int test_managed_lua_client_start(struct lua_State *L,
                                         salts_lua_executor_t *executor,
                                         void *context) {
  TestManagedLuaClientContext *test =
      (TestManagedLuaClientContext *)context;
  if (luaL_dostring(
          L,
          "return { enrich_order_deferred = function(request) "
          "return { id = request.id + 3, symbol = request.symbol .. '-worker' }, nil "
          "end }") != LUA_OK) {
    lua_pop(L, 1);
    return SALTS_EIO;
  }
  test->start_status = Orders_lua_client_create(
      &test->client, executor, -1, &test->limits, &test->error);
  lua_pop(L, 1);
  return test->start_status == DATA_BIND_OK ? SALTS_OK : SALTS_EIO;
}

static int test_managed_lua_client_stop(struct lua_State *L,
                                        salts_lua_executor_t *executor,
                                        void *context) {
  TestManagedLuaClientContext *test =
      (TestManagedLuaClientContext *)context;
  (void)L;
  (void)executor;
  if (test->client == NULL) return SALTS_OK;
  test->close_status =
      Orders_lua_client_close(test->client, &test->error);
  if (test->close_status == DATA_BIND_OK) test->client = NULL;
  return test->close_status == DATA_BIND_OK ? SALTS_OK : SALTS_EIO;
}

static DataBindStatus test_create_order(void *context, const Order_t *request,
                                        OrderResult_t *response,
                                        DataBindError *error) {
  TestOrderApiContext *state = (TestOrderApiContext *)context;
  state->calls++;
  if (state->next_status != DATA_BIND_OK) {
    error->code = state->next_status;
    snprintf(error->path, sizeof(error->path), "%s", "orders.create_order");
    snprintf(error->message, sizeof(error->message), "%s", "order rejected");
    return state->next_status;
  }
  response->id = request->order_id;
  response->symbol = tstr_dup(request->symbol);
  return response->symbol != NULL ? DATA_BIND_OK : DATA_BIND_ERR_OOM;
}

static DataBindStatus test_fetch_order_poll(
    void *state, OrderResult_t *response, int *out_done,
    DataBindError *error) {
  TestAsyncOrderOperation *operation = (TestAsyncOrderOperation *)state;
  (void)error;
  operation->polls++;
  if (operation->polls <= 2u) {
    operation->context->async_steps++;
    *out_done = 0;
    return DATA_BIND_OK;
  }
  response->id = operation->id;
  response->symbol = tstr_dup(operation->symbol);
  if (response->symbol == NULL) return DATA_BIND_ERR_OOM;
  *out_done = 1;
  return DATA_BIND_OK;
}

static void test_fetch_order_destroy(void *state, int canceled) {
  TestAsyncOrderOperation *operation = (TestAsyncOrderOperation *)state;
  operation->context->async_destroys++;
  if (canceled) operation->context->cancellations++;
  tstr_free(operation->symbol);
  free(operation);
}

static void test_invalid_future_destroy(void *state, int canceled) {
  TestOrderApiContext *context = (TestOrderApiContext *)state;
  context->async_destroys++;
  if (canceled) context->cancellations++;
}

static DataBindStatus test_invalid_future(
    void *context, const AsyncOrder_t *request,
    Orders_lua_fetch_order_async_t *operation,
    DataBindError *error) {
  (void)request;
  (void)error;
  operation->state = context;
  operation->destroy = test_invalid_future_destroy;
  return DATA_BIND_OK;
}

static DataBindStatus test_fetch_order(
    void *context, const AsyncOrder_t *request,
    Orders_lua_fetch_order_async_t *operation,
    DataBindError *error) {
  TestOrderApiContext *test = (TestOrderApiContext *)context;
  TestAsyncOrderOperation *state =
      (TestAsyncOrderOperation *)calloc(1u, sizeof(*state));
  (void)error;
  if (state == NULL) return DATA_BIND_ERR_OOM;
  state->symbol = tstr_dup(request->symbol);
  if (state->symbol == NULL) {
    free(state);
    return DATA_BIND_ERR_OOM;
  }
  state->context = test;
  state->id = request->id;
  operation->state = state;
  operation->poll = test_fetch_order_poll;
  operation->destroy = test_fetch_order_destroy;
  test->async_calls++;
  return DATA_BIND_OK;
}

static Orders_lua_api_t test_order_api(TestOrderApiContext *context) {
  Orders_lua_api_t api = {0};
  api.context = context;
  api.create_order = test_create_order;
  api.fetch_order = test_fetch_order;
  return api;
}

static void check_lua_integer_field(lua_State *L, int table_index,
                                    const char *field, lua_Integer expected) {
  lua_getfield(L, table_index, field);
  check_true(lua_isinteger(L, -1));
  check(lua_tointeger(L, -1) == expected);
  lua_pop(L, 1);
}

spec("generated typed Order Lua adapter") {
  static lua_State *L = NULL;
  static DataBind *codec = NULL;
  static DataBindError error = DATA_BIND_ERROR_INIT;
  static Order_t order;
  const char *json =
      "{\"header\":{\"seq\":7},\"legacyId\":42,\"request_id\":\""
      TEST_ORDER_REQUEST_ID
      "\",\"min_value\":-99,\"max_value\":99,\"side\":\"Buy\","
      "\"routing_hint\":9,\"fills\":[{\"price\":100,\"qty\":3}],"
      "\"symbol\":\"ABC\",\"client_tag\":\"edge-a\",\"payload\":\"raw\"}";

  before_each() {
    L = luaL_newstate();
    Order_init(&order);
    check_not_null(L);
    if (L != NULL) luaL_openlibs(L);
    check_equal(Orders_codec_create(&codec, &error), DATA_BIND_OK);
    if (codec != NULL)
      check_equal(Order_from_json(codec, &order, json, strlen(json), &error),
                   DATA_BIND_OK);
  }

  after_each() {
    Order_clear(&order);
    data_bind_free(codec);
    if (L != NULL) lua_close(L);
  }

  it("should copy canonical schema fields and nested values to Lua") {
    size_t payload_len = 0;
    const char *payload;

    check_equal(Order_push_lua(L, &order, 16u), DATA_BIND_OK);
    check_equal(lua_gettop(L), 1);
    check_true(lua_istable(L, -1));
    check_lua_integer_field(L, -1, "id", 42);
    check_lua_integer_field(L, -1, "min_value", -99);
    check_lua_integer_field(L, -1, "max_value", 99);
    check_lua_integer_field(L, -1, "routing_hint", 9);

    lua_getfield(L, -1, "header");
    check_true(lua_istable(L, -1));
    check_lua_integer_field(L, -1, "seq", 7);
    lua_pop(L, 1);

    lua_getfield(L, -1, "fills");
    check_true(lua_istable(L, -1));
    check_equal((int)lua_rawlen(L, -1), 1);
    lua_rawgeti(L, -1, 1);
    check_lua_integer_field(L, -1, "price", 100);
    check_lua_integer_field(L, -1, "qty", 3);
    lua_pop(L, 2);

    lua_getfield(L, -1, "payload");
    payload = lua_tolstring(L, -1, &payload_len);
    check_equal(payload_len, 3u);
    check_equal(payload, "raw", 3u);
    lua_pop(L, 1);

    lua_getfield(L, -1, "request_id");
    check_equal(lua_tostring(L, -1), TEST_ORDER_REQUEST_ID);
    lua_pop(L, 1);
  }

  it("should restore the stack when an integer cannot fit Lua") {
    int base = lua_gettop(L);
    order.max_value = UINT64_MAX;
    check_equal(Order_push_lua(L, &order, 16u), DATA_BIND_ERR_LIMIT);
    check_equal(lua_gettop(L), base);
  }

  it("should transactionally copy a Lua table into an owning record") {
    Order_t decoded;
    const Fill_t *fill;

    Order_init(&decoded);
    check_equal(Order_push_lua(L, &order, 16u), DATA_BIND_OK);
    check_equal(Order_from_lua(L, -1, &decoded, 16u, 64u), DATA_BIND_OK);
    check_equal(lua_gettop(L), 1);
    check_equal(decoded.order_id, 42u);
    check(decoded.min_value == -99);
    check(decoded.max_value == 99u);
    check_equal(decoded.routing_hint, 9u);
    check_equal(decoded.symbol, "ABC");
    check_equal(decoded.client_tag, "edge-a");
    check_equal(Order_fills_vec_t_size(&decoded.fills), 1u);
    fill = Order_fills_vec_t_at_const(&decoded.fills, 0u);
    check_not_null(fill);
    if (fill != NULL) {
      check_equal(fill->price, 100);
      check_equal(fill->qty, 3u);
    }
    Order_clear(&decoded);
  }

  it("should preserve the destination and stack when Lua input is invalid") {
    int base;

    check_equal(Order_push_lua(L, &order, 16u), DATA_BIND_OK);
    lua_pushstring(L, "not-an-integer");
    lua_setfield(L, -2, "id");
    base = lua_gettop(L);
    check_equal(Order_from_lua(L, -1, &order, 16u, 64u),
                 DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(lua_gettop(L), base);
    check_equal(order.order_id, 42u);
    check_equal(order.symbol, "ABC");
  }

  it("should reject unknown Lua record keys without modifying the destination") {
    int base;

    check_equal(Order_push_lua(L, &order, 16u), DATA_BIND_OK);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "unexpected");
    base = lua_gettop(L);
    check_equal(Order_from_lua(L, -1, &order, 16u, 64u),
                 DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(lua_gettop(L), base);
    check_equal(order.order_id, 42u);
    check_equal(order.symbol, "ABC");
  }

  it("should reject Lua containers above the caller limit") {
    int base;

    check_equal(Order_push_lua(L, &order, 16u), DATA_BIND_OK);
    base = lua_gettop(L);
    check_equal(Order_from_lua(L, -1, &order, 16u, 2u), DATA_BIND_ERR_LIMIT);
    check_equal(lua_gettop(L), base);
    check_equal(order.order_id, 42u);
    check_equal(tbe_bytes_t_size(&order.payload), 3u);
  }

  it("should enforce the configured nesting depth") {
    int base = lua_gettop(L);
    check_equal(Order_push_lua(L, &order, 1u), DATA_BIND_ERR_LIMIT);
    check_equal(lua_gettop(L), base);
  }

  it("should call a Lua import with typed C request and response objects") {
    Orders_lua_client_t *client = NULL;
    salts_lua_executor_t *executor = NULL;
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    DataBindError call_error = DATA_BIND_ERROR_INIT;
    LuaOrder_t request;
    OrderResult_t response;
    int base;
    int lua_status;

    LuaOrder_init(&request);
    OrderResult_init(&response);
    request.id = 41u;
    request.symbol = tstr_dup("ABC");
    check_not_null(request.symbol);
    lua_status = luaL_dostring(
        L,
        "return { enrich_order = function(request) "
        "return { id = request.id + 1, symbol = request.symbol .. '-lua' }, nil "
        "end }");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);

    base = lua_gettop(L);
    check_equal(salts_lua_executor_create(&executor, L, NULL), SALTS_OK);
    check_equal(Orders_lua_client_create(&client, executor, -1, &limits,
                                          &call_error),
                 DATA_BIND_OK);
    check_equal(lua_gettop(L), base);
    lua_pop(L, 1);
    base = lua_gettop(L);

    check_equal(Orders_lua_client_enrich_order_on_owner(
                     client, &request, &response, &call_error),
                 DATA_BIND_OK);
    check_equal(lua_gettop(L), base);
    check_equal(response.id, 42u);
    check_equal(response.symbol, "ABC-lua");

    check_equal(Orders_lua_client_close(client, &call_error), DATA_BIND_OK);
    check_equal(salts_lua_executor_destroy(executor), SALTS_OK);
    OrderResult_clear(&response);
    LuaOrder_clear(&request);
  }

  it("should reject generated Lua client control from a non-owner thread") {
    Orders_lua_client_t *client = NULL;
    salts_lua_executor_t *executor = NULL;
    salts_thread_t thread = NULL;
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    DataBindError call_error = DATA_BIND_ERROR_INIT;
    LuaOrder_t request;
    OrderResult_t response;
    TestLuaClientNonOwnerContext context = {0};

    LuaOrder_init(&request);
    OrderResult_init(&response);
    request.id = 41u;
    request.symbol = tstr_dup("OWNER");
    check_not_null(request.symbol);
    check_equal(
        luaL_dostring(
            L,
            "return { enrich_order = function(request) "
            "return { id = request.id, symbol = request.symbol }, nil end }"),
        LUA_OK);
    check_equal(salts_lua_executor_create(&executor, L, NULL), SALTS_OK);
    check_equal(Orders_lua_client_create(&client, executor, -1, &limits,
                                          &call_error),
                 DATA_BIND_OK);
    lua_pop(L, 1);

    context.client = client;
    context.request = &request;
    context.response = &response;
    context.call_error = (DataBindError)DATA_BIND_ERROR_INIT;
    context.close_error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(salts_thread_create(&thread, test_lua_client_non_owner,
                                     &context),
                 0);
    check_equal(salts_thread_join(&thread), 0);
    check_equal(context.call_status, DATA_BIND_ERR_RUNTIME);
    check_equal(context.call_error.path, "executor.owner");
    check_equal(context.close_status, DATA_BIND_ERR_RUNTIME);
    check_equal(context.close_error.path, "executor.owner");

    check_equal(Orders_lua_client_close(client, &call_error), DATA_BIND_OK);
    check_equal(salts_lua_executor_destroy(executor), SALTS_OK);
    OrderResult_clear(&response);
    LuaOrder_clear(&request);
  }

  it("should execute a copied typed Lua import through the bounded executor") {
    Orders_lua_client_t *client = NULL;
    Orders_lua_enrich_order_deferred_future_t *future = NULL;
    salts_lua_executor_t *executor = NULL;
    salts_thread_t producer = NULL;
    salts_lua_executor_config_t executor_config =
        SALTS_LUA_EXECUTOR_CONFIG_DEFAULT;
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    DataBindError call_error = DATA_BIND_ERROR_INIT;
    AsyncLuaOrder_t request;
    OrderResult_t response;
    TestLuaClientProducerContext producer_context = {0};
    size_t processed = 0u;
    int done = 1;

    AsyncLuaOrder_init(&request);
    OrderResult_init(&response);
    request.id = 10u;
    request.symbol = tstr_dup("ASYNC");
    check_not_null(request.symbol);
    check_equal(
        luaL_dostring(
            L,
            "return { enrich_order_deferred = function(request) "
            "return { id = request.id + 5, symbol = request.symbol .. '-lua' }, nil "
            "end }"),
        LUA_OK);
    executor_config.queue_capacity = 2u;
    check_equal(salts_lua_executor_create(&executor, L, &executor_config),
                 SALTS_OK);
    check_equal(Orders_lua_client_create(&client, executor, -1, &limits,
                                          &call_error),
                 DATA_BIND_OK);
    lua_pop(L, 1);

    producer_context.client = client;
    producer_context.request = &request;
    producer_context.error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        salts_thread_create(&producer, test_lua_client_producer,
                            &producer_context),
        0);
    check_equal(salts_thread_join(&producer), 0);
    check_equal(producer_context.status, DATA_BIND_OK);
    future = producer_context.future;
    check_not_null(future);
    AsyncLuaOrder_clear(&request);
    check_false(Orders_lua_enrich_order_deferred_future_done(future));
    check_equal(Orders_lua_enrich_order_deferred_future_poll(
                     future, &done, &response, &call_error),
                 DATA_BIND_OK);
    check_false(done);

    check_equal(salts_lua_executor_poll(executor, 1u, &processed), SALTS_OK);
    check_equal(processed, 1u);
    check_true(Orders_lua_enrich_order_deferred_future_done(future));
    check_equal(Orders_lua_enrich_order_deferred_future_poll(
                     future, &done, &response, &call_error),
                 DATA_BIND_OK);
    check_true(done);
    check_equal(response.id, 15u);
    check_equal(response.symbol, "ASYNC-lua");

    Orders_lua_enrich_order_deferred_future_destroy(future);
    check_equal(Orders_lua_client_close(client, &call_error), DATA_BIND_OK);
    check_equal(salts_lua_executor_destroy(executor), SALTS_OK);
    OrderResult_clear(&response);
  }

  it("should drain a generated async client on a managed Lua worker") {
    salts_lua_worker_t *managed = NULL;
    salts_lua_worker_config_t config = SALTS_LUA_WORKER_CONFIG_DEFAULT;
    Orders_lua_enrich_order_deferred_future_t *future = NULL;
    TestManagedLuaClientContext context = {
        .limits = {16u, 64u, 4u},
        .error = DATA_BIND_ERROR_INIT,
        .start_status = DATA_BIND_ERR_RUNTIME,
        .close_status = DATA_BIND_ERR_RUNTIME};
    AsyncLuaOrder_t request;
    OrderResult_t response;
    DataBindError future_error = DATA_BIND_ERROR_INIT;
    int done = 0;

    AsyncLuaOrder_init(&request);
    OrderResult_init(&response);
    request.id = 20u;
    request.symbol = tstr_dup("ASYNC");
    check_not_null(request.symbol);
    config.on_start = test_managed_lua_client_start;
    config.on_stop = test_managed_lua_client_stop;
    config.context = &context;
    check_equal(salts_lua_worker_create(&managed, &config), SALTS_OK);
    check_equal(context.start_status, DATA_BIND_OK);

    check_equal(Orders_lua_client_enrich_order_deferred_async(
                     context.client, &request, &future, &future_error),
                 DATA_BIND_OK);
    AsyncLuaOrder_clear(&request);
    check_equal(salts_lua_worker_stop(
                     managed, SALTS_LUA_EXECUTOR_SHUTDOWN_DRAIN),
                 SALTS_OK);
    check_equal(context.close_status, DATA_BIND_OK);
    check_null(context.client);
    check_true(Orders_lua_enrich_order_deferred_future_done(future));
    check_equal(Orders_lua_enrich_order_deferred_future_poll(
                     future, &done, &response, &future_error),
                 DATA_BIND_OK);
    check_true(done);
    check_equal(response.id, 23u);
    check_equal(response.symbol, "ASYNC-worker");

    Orders_lua_enrich_order_deferred_future_destroy(future);
    check_equal(salts_lua_worker_destroy(managed), SALTS_OK);
    OrderResult_clear(&response);
  }

  it("should expose async Lua import backpressure and cancellation") {
    Orders_lua_client_t *client = NULL;
    Orders_lua_enrich_order_deferred_future_t *first = NULL;
    Orders_lua_enrich_order_deferred_future_t *second = NULL;
    salts_lua_executor_t *executor = NULL;
    salts_lua_executor_config_t executor_config =
        SALTS_LUA_EXECUTOR_CONFIG_DEFAULT;
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    DataBindError call_error = DATA_BIND_ERROR_INIT;
    AsyncLuaOrder_t request;
    OrderResult_t response;
    size_t processed = 0u;
    int done = 0;

    AsyncLuaOrder_init(&request);
    OrderResult_init(&response);
    request.symbol = tstr_dup("CANCEL");
    check_not_null(request.symbol);
    check_equal(
        luaL_dostring(
            L,
            "return { enrich_order_deferred = function(request) "
            "return { id = request.id, symbol = request.symbol }, nil end }"),
        LUA_OK);
    executor_config.queue_capacity = 1u;
    check_equal(salts_lua_executor_create(&executor, L, &executor_config),
                 SALTS_OK);
    check_equal(Orders_lua_client_create(&client, executor, -1, &limits,
                                          &call_error),
                 DATA_BIND_OK);
    lua_pop(L, 1);

    check_equal(Orders_lua_client_enrich_order_deferred_async(
                     client, &request, &first, &call_error),
                 DATA_BIND_OK);
    check_equal(Orders_lua_client_enrich_order_deferred_async(
                     client, &request, &second, &call_error),
                 DATA_BIND_ERR_LIMIT);
    check_null(second);
    check_equal(call_error.path, "executor");
    check_true(Orders_lua_enrich_order_deferred_future_cancel(first));
    check_equal(Orders_lua_enrich_order_deferred_future_poll(
                     first, &done, &response, &call_error),
                 DATA_BIND_ERR_CANCELED);
    check_true(done);
    check_equal(salts_lua_executor_poll(executor, 1u, &processed), SALTS_OK);
    check_equal(processed, 1u);

    Orders_lua_enrich_order_deferred_future_destroy(first);
    check_equal(Orders_lua_client_close(client, &call_error), DATA_BIND_OK);
    check_equal(salts_lua_executor_destroy(executor), SALTS_OK);
    OrderResult_clear(&response);
    AsyncLuaOrder_clear(&request);
  }

  it("should preserve the C response and stack when a Lua import reports an error") {
    Orders_lua_client_t *client = NULL;
    salts_lua_executor_t *executor = NULL;
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    DataBindError call_error = DATA_BIND_ERROR_INIT;
    LuaOrder_t request;
    OrderResult_t response;
    int base;

    LuaOrder_init(&request);
    OrderResult_init(&response);
    request.id = 7u;
    request.symbol = tstr_dup("request");
    response.id = 99u;
    response.symbol = tstr_dup("kept");
    check_not_null(request.symbol);
    check_not_null(response.symbol);
    check_equal(
        luaL_dostring(
            L,
            "return { enrich_order = function(_) "
            "return nil, { code = 8, path = 'lua.enrich_order', "
            "message = 'order rejected' } end }"),
        LUA_OK);
    check_equal(salts_lua_executor_create(&executor, L, NULL), SALTS_OK);
    check_equal(Orders_lua_client_create(&client, executor, -1, &limits,
                                          &call_error),
                 DATA_BIND_OK);
    lua_pop(L, 1);
    base = lua_gettop(L);

    check_equal(Orders_lua_client_enrich_order_on_owner(
                     client, &request, &response, &call_error),
                 DATA_BIND_ERR_RUNTIME);
    check_equal(lua_gettop(L), base);
    check_equal(response.id, 99u);
    check_equal(response.symbol, "kept");
    check_equal(call_error.code, DATA_BIND_ERR_RUNTIME);
    check_equal(call_error.path, "lua.enrich_order");
    check_equal(call_error.message, "order rejected");

    check_equal(Orders_lua_client_close(client, &call_error), DATA_BIND_OK);
    check_equal(salts_lua_executor_destroy(executor), SALTS_OK);
    OrderResult_clear(&response);
    LuaOrder_clear(&request);
  }

  it("should preserve the C response when a Lua import returns the wrong type") {
    Orders_lua_client_t *client = NULL;
    salts_lua_executor_t *executor = NULL;
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    DataBindError call_error = DATA_BIND_ERROR_INIT;
    LuaOrder_t request;
    OrderResult_t response;
    int base;

    LuaOrder_init(&request);
    OrderResult_init(&response);
    response.id = 77u;
    response.symbol = tstr_dup("unchanged");
    check_not_null(response.symbol);
    check_equal(
        luaL_dostring(
            L,
            "return { enrich_order = function(_) "
            "return { id = 'wrong', symbol = 'invalid' }, nil end }"),
        LUA_OK);
    check_equal(salts_lua_executor_create(&executor, L, NULL), SALTS_OK);
    check_equal(Orders_lua_client_create(&client, executor, -1, &limits,
                                          &call_error),
                 DATA_BIND_OK);
    lua_pop(L, 1);
    base = lua_gettop(L);

    check_equal(Orders_lua_client_enrich_order_on_owner(
                     client, &request, &response, &call_error),
                 DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(lua_gettop(L), base);
    check_equal(response.id, 77u);
    check_equal(response.symbol, "unchanged");
    check_equal(call_error.code, DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(call_error.path, "response");

    check_equal(Orders_lua_client_close(client, &call_error), DATA_BIND_OK);
    check_equal(salts_lua_executor_destroy(executor), SALTS_OK);
    OrderResult_clear(&response);
    LuaOrder_clear(&request);
  }

  it("should fail atomically without invoking a module index metamethod") {
    Orders_lua_client_t *client = NULL;
    salts_lua_executor_t *executor = NULL;
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    DataBindError call_error = DATA_BIND_ERROR_INIT;
    LuaOrder_t request;
    OrderResult_t response;
    int base;

    LuaOrder_init(&request);
    OrderResult_init(&response);
    check_equal(
        luaL_dostring(
            L,
            "return setmetatable({}, { __index = function() "
            "error('module index escaped protected call') end })"),
        LUA_OK);
    check_equal(salts_lua_executor_create(&executor, L, NULL), SALTS_OK);
    check_equal(Orders_lua_client_create(&client, executor, -1, &limits,
                                          &call_error),
                 DATA_BIND_OK);
    lua_pop(L, 1);
    base = lua_gettop(L);

    check_equal(Orders_lua_client_enrich_order_on_owner(
                     client, &request, &response, &call_error),
                 DATA_BIND_ERR_RUNTIME);
    check_equal(lua_gettop(L), base);
    check_equal(call_error.code, DATA_BIND_ERR_RUNTIME);
    check_equal(call_error.path, "enrich_order");
    check_contains(call_error.message, "missing");

    check_equal(Orders_lua_client_close(client, &call_error), DATA_BIND_OK);
    check_equal(salts_lua_executor_destroy(executor), SALTS_OK);
    OrderResult_clear(&response);
    LuaOrder_clear(&request);
  }

  it("should expose a schema operation as a typed Lua module function") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local result, err = orders.create_order({"
        "header={seq=7}, id=42, min_value=-99, max_value=99, "
        "request_id='" TEST_ORDER_REQUEST_ID "', side=1, "
        "fills={{price=100, qty=3}}, symbol='ABC', payload='raw'"
        "}); assert(err == nil, err and err.message); assert(result.id == 42); "
        "assert(result.symbol == 'ABC')");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.calls, 1u);
  }

  it("should return structured Lua errors without bypassing cleanup") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    context.next_status = DATA_BIND_ERR_RUNTIME;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local result, err = orders.create_order({"
        "header={seq=7}, id=42, min_value=-99, max_value=99, "
        "request_id='" TEST_ORDER_REQUEST_ID "', side=1, "
        "fills={{price=100, qty=3}}, symbol='ABC', payload='raw'"
        "}); assert(result == nil); "
        "assert(err.code == 8); "
        "assert(err.path == 'orders.create_order'); "
        "assert(err.message == 'order rejected')");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.calls, 1u);
  }

  it("should reject an invalid request before entering the callback") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local result, err = orders.create_order({id='wrong'}); "
        "assert(result == nil); assert(err.code == 6); "
        "assert(err.path == 'request')");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.calls, 0u);
  }

  it("should advance one explicit state-machine step per poll") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local future, create_err = orders.fetch_order({id=7, symbol='XYZ'}); "
        "assert(create_err == nil); assert(not future:done()); "
        "local done, result, err = future:poll(); "
        "assert(not done and result == nil and err == nil); "
        "done, result, err = future:poll(); "
        "assert(not done and result == nil and err == nil); "
        "done, result, err = future:poll(); "
        "assert(done and err == nil); assert(result.id == 7); "
        "assert(result.symbol == 'XYZ'); assert(future:done())");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.async_calls, 1u);
    check_equal(context.async_steps, 2u);
    check_equal(context.async_destroys, 1u);
    check_equal(context.cancellations, 0u);
  }

  it("should reject incomplete explicit Future hooks without leaking state") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    api.fetch_order = test_invalid_future;
    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local future, err = orders.fetch_order({id=22, symbol='BAD'}); "
        "assert(future == nil and err.code == 1 and err.path == 'operation')");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.async_destroys, 1u);
    check_equal(context.cancellations, 1u);
  }

  it("should await an async operation through Lua coroutine continuations") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local co = coroutine.create(function() "
        "  local future = assert(orders.fetch_order({id=9, symbol='AWAIT'})); "
        "  local result, err = future:await(); assert(err == nil); "
        "  return result.id, result.symbol "
        "end); "
        "local ok, first = coroutine.resume(co); assert(ok, first); "
        "assert(type(first) == 'userdata'); "
        "assert(coroutine.status(co) == 'suspended'); "
        "local ok2, second = coroutine.resume(co); assert(ok2, second); "
        "assert(rawequal(first, second)); "
        "assert(coroutine.status(co) == 'suspended'); "
        "local ok3, id, symbol = coroutine.resume(co); assert(ok3, id); "
        "assert(id == 9 and symbol == 'AWAIT'); "
        "assert(coroutine.status(co) == 'dead')");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.async_calls, 1u);
    check_equal(context.async_steps, 2u);
    check_equal(context.async_destroys, 1u);
    check_equal(context.cancellations, 0u);
  }

  it("should reject await on the main Lua thread without advancing") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local future = assert(orders.fetch_order({id=10, symbol='MAIN'})); "
        "local result, err = future:await(); "
        "assert(result == nil and err.code == 1 and err.path == 'await'); "
        "assert(not future:done()); assert(future:cancel())");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.async_calls, 1u);
    check_equal(context.async_steps, 0u);
    check_equal(context.async_destroys, 1u);
    check_equal(context.cancellations, 1u);
  }

  it("should cancel await when its suspended Lua coroutine is collected") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local co = coroutine.create(function() "
        "  local future = assert(orders.fetch_order({id=11, symbol='DROP'})); "
        "  return future:await() "
        "end); "
        "local ok, token = coroutine.resume(co); assert(ok, token); "
        "co = nil; token = nil; "
        "collectgarbage('collect'); collectgarbage('collect')");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.async_calls, 1u);
    check_equal(context.async_steps, 1u);
    check_equal(context.cancellations, 1u);
  }

  it("should cancel a suspended async operation exactly once") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local future = assert(orders.fetch_order({id=8, symbol='CXL'})); "
        "local done = future:poll(); assert(not done); "
        "assert(future:cancel()); assert(not future:cancel()); "
        "assert(future:done()); "
        "local completed, result, err = future:poll(); "
        "assert(completed and result == nil and err.code == 11)");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.async_calls, 1u);
    check_equal(context.cancellations, 1u);
  }

  it("should enforce the pending async operation limit") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 1u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local first = assert(orders.fetch_order({id=1, symbol='A'})); "
        "local second, err = orders.fetch_order({id=2, symbol='B'}); "
        "assert(second == nil and err.code == 9); assert(first:cancel())");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.cancellations, 1u);
  }

  it("should cancel a suspended future during Lua collection") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "local future = assert(orders.fetch_order({id=3, symbol='GC'})); "
        "assert(not future:poll()); future = nil; collectgarbage('collect')");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    check_equal(context.cancellations, 1u);
  }

  it("should cancel pending futures before the Lua module is destroyed") {
    TestOrderApiContext context = {0};
    Orders_lua_api_t api = test_order_api(&context);
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int lua_status;

    check_equal(Orders_lua_push_module(L, &api, &limits), DATA_BIND_OK);
    lua_setglobal(L, "orders");
    lua_status = luaL_dostring(
        L,
        "pending = assert(orders.fetch_order({id=4, symbol='CLOSE'})); "
        "assert(not pending:poll())");
    if (lua_status != LUA_OK) info("Lua error: %s", lua_tostring(L, -1));
    check_equal(lua_status, LUA_OK);
    lua_close(L);
    L = NULL;
    check_equal(context.cancellations, 1u);
  }

  it("should fail module creation atomically for an incomplete API") {
    Orders_lua_api_t api = {0};
    Orders_lua_limits_t limits = {16u, 64u, 4u};
    int base = lua_gettop(L);

    check_equal(Orders_lua_push_module(L, &api, &limits),
                 DATA_BIND_ERR_INVALID_ARG);
    check_equal(lua_gettop(L), base);
  }
}

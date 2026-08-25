#include "cbind_standalone.h"

#include <turbo/thread.h>
#include <turbo_cmeta_data.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_UUID "00112233-4455-6677-8899-aabbccddeeff"
#define TEST_NESTED_UUID "FEDCBA98-7654-3210-FEDC-BA9876543210"

enum {
  TEST_THREAD_COUNT = 12,
  TEST_SCRATCH_BYTES = 2,
  TEST_MAX_DEPTH = 2,
  TEST_MAX_BUFFER_BYTES = sizeof(TEST_UUID) - 1u
};

#define TEST_MAP_BEGIN {.kind = CSERDE_MAP_BEGIN}
#define TEST_MAP_END {.kind = CSERDE_MAP_END}
#define TEST_TEXT(text_)                                                                           \
  {                                                                                                \
    .kind = CSERDE_STRING, .value.slice = {                                                        \
      (const unsigned char *)(text_), sizeof(text_) - 1u, CSERDE_VIEW_STABLE                       \
    }                                                                                              \
  }
#define TEST_SINT(value_) {.kind = CSERDE_SINT, .value.sint = (value_)}
#define TEST_UINT(value_) {.kind = CSERDE_UINT, .value.uint = (value_)}
#define TEST_BOOL(value_) {.kind = CSERDE_BOOL, .value.boolean = (value_)}

static const cserde_token test_tokens[] = {
    TEST_MAP_BEGIN, TEST_TEXT("details"), TEST_MAP_BEGIN, TEST_TEXT("sequence"),
    TEST_SINT(-17), TEST_TEXT("nested_request_id"), TEST_TEXT(TEST_NESTED_UUID), TEST_MAP_END,
    TEST_TEXT("eventId"), TEST_SINT(42), TEST_TEXT("enabled"), TEST_BOOL(true),
    TEST_TEXT("min_value"), TEST_SINT(INT64_MIN), TEST_TEXT("max_value"),
    TEST_UINT(UINT64_MAX), TEST_TEXT("label"), TEST_TEXT("owned"), TEST_TEXT("request_id"),
    TEST_TEXT(TEST_UUID), TEST_TEXT("state"), TEST_TEXT("Ready"), TEST_MAP_END};

typedef struct test_reader_state {
  size_t position;
} test_reader_state;

typedef struct test_worker {
  unsigned int role;
  int result;
} test_worker;

static atomic_uint test_ready_count;
static atomic_bool test_start;

static cserde_status test_reader_next(void *opaque, cserde_token *out) {
  test_reader_state *state = (test_reader_state *)opaque;
  if (state == NULL || out == NULL ||
      state->position >= sizeof(test_tokens) / sizeof(test_tokens[0]))
    return CSERDE_SOURCE_ERROR;
  *out = test_tokens[state->position++];
  return CSERDE_OK;
}

static const cserde_reader_ops test_reader_ops = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION, test_reader_next};

static int test_uuid_slot_is_canonical(const cmeta_data_desc *descriptor,
                                       size_t field_index) {
  const cmeta_data_struct_shape *shape;

  if (!cmeta_data_desc_valid(descriptor) || descriptor->kind != CMETA_DATA_STRUCT)
    return 0;
  shape = (const cmeta_data_struct_shape *)descriptor->shape;
  return shape != NULL && shape->layout != NULL && field_index < shape->field_count &&
         field_index < shape->layout->field_count &&
         shape->layout->fields[field_index].type == &turbo_uuid_cmeta_type &&
         shape->fields[field_index].value == &turbo_uuid_cmeta_data;
}

static int test_decode(void) {
  unsigned char scratch[TEST_SCRATCH_BYTES] = {0};
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, sizeof(scratch), TEST_MAX_DEPTH, 0u, TEST_MAX_BUFFER_BYTES);
  cbind_error error = CBIND_ERROR_INIT;
  test_reader_state state = {0};
  cserde_reader reader = {0};
  CBindStandaloneEnvelope_t envelope = {0};
  turbo_uuid_t expected_uuid = {{0}};
  turbo_uuid_t expected_nested_uuid = {{0}};
  int result = 0;

  if (turbo_uuid_parse(TEST_UUID, &expected_uuid) != 0 ||
      turbo_uuid_parse(TEST_NESTED_UUID, &expected_nested_uuid) != 0 ||
      cserde_reader_init(&reader, &test_reader_ops, &state) != CSERDE_OK ||
      CBindStandaloneEnvelope_from_cserde(&context, &reader, &envelope, &error) != CBIND_OK ||
      !turbo_uuid_equal(&envelope.request_id, &expected_uuid) ||
      !turbo_uuid_equal(&envelope.details.nested_request_id, &expected_nested_uuid))
    result = 1;
  tstr_freep(&envelope.label);
  return result;
}

static void test_worker_run(void *opaque) {
  test_worker *worker = (test_worker *)opaque;

  atomic_fetch_add_explicit(&test_ready_count, 1u, memory_order_release);
  while (!atomic_load_explicit(&test_start, memory_order_acquire)) turbo_thread_yield();

  if (worker->role == 0u)
    worker->result = cmeta_data_desc_valid(CBindStandaloneEnvelope_cbind_data()) ? 0 : 1;
  else if (worker->role == 1u)
    worker->result = cmeta_data_desc_valid(CBindStandaloneDetails_cbind_data()) ? 0 : 1;
  else
    worker->result = test_decode();
}

int main(void) {
  turbo_thread_t threads[TEST_THREAD_COUNT] = {0};
  test_worker workers[TEST_THREAD_COUNT];
  size_t created = 0u;
  size_t index;
  int worker_failed = 0;

  for (index = 0u; index < TEST_THREAD_COUNT; ++index) {
    workers[index] = (test_worker){(unsigned int)(index % 3u), 1};
    if (turbo_thread_create(&threads[index], test_worker_run, &workers[index]) != 0) break;
    ++created;
  }
  if (created != TEST_THREAD_COUNT) {
    atomic_store_explicit(&test_start, true, memory_order_release);
    for (index = 0u; index < created; ++index) (void)turbo_thread_join(&threads[index]);
    fprintf(stderr, "failed to create every CBind initialization worker\n");
    return 1;
  }
  while (atomic_load_explicit(&test_ready_count, memory_order_acquire) != TEST_THREAD_COUNT)
    turbo_thread_yield();
  atomic_store_explicit(&test_start, true, memory_order_release);

  for (index = 0u; index < TEST_THREAD_COUNT; ++index) {
    if (turbo_thread_join(&threads[index]) != 0 || workers[index].result != 0)
      worker_failed = 1;
  }
  if (worker_failed) {
    fprintf(stderr, "mixed CBind accessor/decode worker failed\n");
    return 2;
  }
  if (!test_uuid_slot_is_canonical(CBindStandaloneEnvelope_cbind_data(), 6u) ||
      !test_uuid_slot_is_canonical(CBindStandaloneDetails_cbind_data(), 1u) ||
      turbo_uuid_cmeta_data.storage_type != &turbo_uuid_cmeta_type ||
      turbo_uuid_cmeta_data.shape != &turbo_uuid_cmeta_shape ||
      turbo_uuid_cmeta_data.buffer_ops != &turbo_uuid_cmeta_buffer_ops) {
    fprintf(stderr, "concurrent publication did not retain canonical UUID metadata\n");
    return 3;
  }
  return 0;
}

#undef TEST_BOOL
#undef TEST_UINT
#undef TEST_SINT
#undef TEST_TEXT
#undef TEST_MAP_END
#undef TEST_MAP_BEGIN
#undef TEST_NESTED_UUID
#undef TEST_UUID

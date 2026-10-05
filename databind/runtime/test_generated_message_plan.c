#include "binary_tail_only_generated.h"
#include "data_bind_message_plan.h"
#include "tinytest.h"

#include <salts_thread.h>
#include <tstr.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum { PREPARED_TEST_THREADS = 4u, PREPARED_TEST_REPEATS = 40u };
static atomic_size_t resolver_calls;
static atomic_int reject_resolver;
static DataBind *codec;
static DataBindError error;

static DataBindStatus counted_binding(DataBindNativeTypeBinding *out, DataBindError *binding_error) {
  atomic_fetch_add_explicit(&resolver_calls, 1u, memory_order_relaxed);
  if (atomic_load_explicit(&reject_resolver, memory_order_relaxed))
    return DATA_BIND_ERR_OOM;
  return TailOnly_native_artifact()->native_binding(out, binding_error);
}

static const DataBindMessageNativeArtifact COUNTED_ARTIFACT = {
    sizeof(DataBindMessageNativeArtifact), DATA_BIND_MESSAGE_NATIVE_ARTIFACT_ABI_VERSION,
    "TailOnly", counted_binding};

typedef struct PrepareWorker {
  DataBind *codec;
  atomic_int *start;
  const DataBindMessagePlan *plan;
  DataBindStatus status;
} PrepareWorker;

static void prepare_worker(void *context) {
  PrepareWorker *worker = (PrepareWorker *)context;
  while (!atomic_load_explicit(worker->start, memory_order_acquire)) salts_thread_yield();
  worker->status = data_bind_message_plan_acquire_generated(
      worker->codec, &COUNTED_ARTIFACT, &worker->plan, NULL);
}

spec("Generated codec-owned MessagePlan preparation") {
  before_each() {
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    atomic_store(&resolver_calls, 0u);
    atomic_store(&reject_resolver, 0);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
  }
  after_each() { data_bind_free(codec); codec = NULL; }

  it("publishes one complete native plan to concurrent cold callers") {
    PrepareWorker workers[PREPARED_TEST_THREADS] = {0};
    salts_thread_t threads[PREPARED_TEST_THREADS] = {0};
    atomic_int start = 0;
    size_t created = 0u;
    const DataBindMessagePlan *warm = NULL;
    size_t cold_calls;
    for (; created < PREPARED_TEST_THREADS; ++created) {
      workers[created].codec = codec;
      workers[created].start = &start;
      if (salts_thread_create(&threads[created], prepare_worker, &workers[created]) != 0)
        break;
    }
    atomic_store_explicit(&start, 1, memory_order_release);
    for (size_t i = 0u; i < created; ++i)
      check_equal(salts_thread_join(&threads[i]), 0);
    check_equal(created, (size_t)PREPARED_TEST_THREADS);
    for (size_t i = 0u; i < created; ++i) {
      check_equal(workers[i].status, DATA_BIND_OK);
      check_not_null(workers[i].plan);
      check_true(workers[i].plan == workers[0].plan);
      check_equal(data_bind_message_plan_field_count(workers[i].plan), (size_t)1u);
    }
    cold_calls = atomic_load(&resolver_calls);
    check_greater_equal(cold_calls, (size_t)1u);
    check_less_equal(cold_calls, (size_t)PREPARED_TEST_THREADS);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &COUNTED_ARTIFACT, &warm, &error), DATA_BIND_OK);
    check_true(warm == workers[0].plan);
    check_equal(atomic_load(&resolver_calls), cold_calls);
  }

  it("keeps a stable plan without resolving the binding again on warm acquisition") {
    const DataBindMessagePlan *first = NULL, *next = NULL;
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &COUNTED_ARTIFACT, &first, &error), DATA_BIND_OK);
    for (size_t i = 0u; i < PREPARED_TEST_REPEATS; ++i) {
      check_equal(data_bind_message_plan_acquire_generated(
          codec, &COUNTED_ARTIFACT, &next, &error), DATA_BIND_OK);
      check_true(next == first);
      check_equal(data_bind_message_plan_native_binding(next)->idl_type_name, "TailOnly");
    }
    check_equal(atomic_load(&resolver_calls), (size_t)1u);
  }

  it("owns the copied binding record independently of caller storage and the codec") {
    DataBindNativeTypeBinding binding = DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
    char binding_name[] = "TailOnly";
    DataBindMessagePlan *owned = NULL;
    DataBindMessagePlanDiagnostic diagnostic = DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    TailOnly_t value;
    check_equal(counted_binding(&binding, &error), DATA_BIND_OK);
    binding.idl_type_name = binding_name;
    check_equal(data_bind_message_plan_compile(
        codec, binding_name, &binding, &owned, &diagnostic), DATA_BIND_OK);
    memset(binding_name, 0, sizeof(binding_name));
    memset(&binding, 0, sizeof(binding));
    data_bind_free(codec);
    codec = NULL;
    TailOnly_init(&value);
    check_equal(data_bind_message_plan_validate_native(
        owned, &value, sizeof(value), &error), DATA_BIND_OK);
    check_equal(data_bind_message_plan_native_binding(owned)->idl_type_name, "TailOnly");
    TailOnly_clear(&value);
    data_bind_message_plan_free(owned);
  }

  it("keeps each codec's validation facts separate for the same generated artifact") {
    static const char constrained[] =
        "message MixedWire { @Min(2) uint64 number; int16 delta; string text; bytes payload; }";
    DataBind *other = NULL;
    const DataBindMessagePlan *normal = NULL, *limited = NULL;
    MixedWire_t value;
    check_equal(data_bind_create_from_text(constrained, sizeof(constrained) - 1u,
        &other, &error), DATA_BIND_OK);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, MixedWire_native_artifact(), &normal, &error), DATA_BIND_OK);
    check_equal(data_bind_message_plan_acquire_generated(
        other, MixedWire_native_artifact(), &limited, &error), DATA_BIND_OK);
    check_true(normal != limited);
    MixedWire_init(&value);
    value.number = 1u;
    check_equal(data_bind_message_plan_validate_native(
        normal, &value, sizeof(value), &error), DATA_BIND_OK);
    check_equal(data_bind_message_plan_validate_native(
        limited, &value, sizeof(value), &error), DATA_BIND_ERR_VALIDATION);
    value.number = 2u;
    check_equal(data_bind_message_plan_validate_native(
        limited, &value, sizeof(value), &error), DATA_BIND_OK);
    MixedWire_clear(&value);
    data_bind_free(other);
  }

  it("publishes nothing on resolver or schema failure and retries only the requested path") {
    DataBindMessageNativeArtifact artifact = COUNTED_ARTIFACT;
    const DataBindMessagePlan *plan = NULL;
    atomic_store(&reject_resolver, 1);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifact, &plan, &error), DATA_BIND_ERR_OOM);
    check_null(plan);
    check_equal(error.code, DATA_BIND_ERR_OOM);
    check_equal(error.path, "TailOnly");
    atomic_store(&reject_resolver, 0);
    artifact.type_name = "Missing";
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifact, &plan, &error), DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    artifact.type_name = "TailOnly";
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifact, &plan, &error), DATA_BIND_OK);
    check_not_null(plan);
    check_equal(atomic_load(&resolver_calls), (size_t)3u);
    data_bind_free(codec);
    codec = NULL;
  }

  it("rejects incomplete or wrong-ABI artifacts before invoking the resolver") {
    DataBindMessageNativeArtifact artifact = COUNTED_ARTIFACT;
    const DataBindMessagePlan *plan = NULL;
    artifact.size = offsetof(DataBindMessageNativeArtifact, native_binding);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifact, &plan, &error), DATA_BIND_ERR_INVALID_ARG);
    check_null(plan);
    artifact.size = sizeof(artifact);
    artifact.abi_version = 0u;
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifact, &plan, &error), DATA_BIND_ERR_INVALID_ARG);
    check_null(plan);
    check_equal(atomic_load(&resolver_calls), (size_t)0u);
  }

  it("rejects a new artifact at the hard plan limit while keeping existing plans usable") {
    const size_t count = DATA_BIND_MESSAGE_PLAN_MAX_PREPARED + 1u;
    DataBindMessageNativeArtifact *artifacts = calloc(count, sizeof(*artifacts));
    const DataBindMessagePlan *first = NULL, *plan = NULL;
    check_not_null(artifacts);
    if (artifacts == NULL) return;
    for (size_t i = 0u; i < count; ++i) artifacts[i] = COUNTED_ARTIFACT;
    for (size_t i = 0u; i < DATA_BIND_MESSAGE_PLAN_MAX_PREPARED; ++i) {
      check_equal(data_bind_message_plan_acquire_generated(
          codec, &artifacts[i], &plan, &error), DATA_BIND_OK);
      if (i == 0u) first = plan;
    }
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifacts[count - 1u], &plan, &error), DATA_BIND_ERR_LIMIT);
    check_null(plan);
    check_equal(atomic_load(&resolver_calls), (size_t)DATA_BIND_MESSAGE_PLAN_MAX_PREPARED);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifacts[0], &plan, &error), DATA_BIND_OK);
    check_true(plan == first);
    data_bind_free(codec);
    codec = NULL;
    free(artifacts);
  }

  it("reuses the prepared plan across generated Binary calls with independent owners") {
    static const uint8_t expected[] = {3u, 0u, 0u, 0u, 'c', 'a', 't'};
    const DataBindMessagePlan *before = NULL, *after = NULL;
    TailOnly_t source, decoded;
    uint8_t wire[sizeof(expected)];
    size_t length = 0u;
    TailOnly_init(&source);
    TailOnly_init(&decoded);
    source.text = tstr_dup("cat");
    check_not_null(source.text);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, TailOnly_native_artifact(), &before, &error), DATA_BIND_OK);
    for (size_t i = 0u; i < PREPARED_TEST_REPEATS; ++i) {
      check_equal(TailOnly_to_bin_into(codec, &source, wire, sizeof(wire),
          &length, &error), DATA_BIND_OK);
      check_equal(length, sizeof(expected));
      check_equal(wire, expected, sizeof(expected));
      check_equal(TailOnly_from_bin(codec, &decoded, wire, length, &error), DATA_BIND_OK);
      check_true(decoded.text != source.text);
      check_equal(decoded.text, "cat");
      TailOnly_clear(&decoded);
    }
    check_equal(data_bind_message_plan_acquire_generated(
        codec, TailOnly_native_artifact(), &after, &error), DATA_BIND_OK);
    check_true(before == after);
    TailOnly_clear(&source);
    TailOnly_clear(&decoded);
  }
}

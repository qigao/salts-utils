#include "binary_tail_only_generated.h"
#include "data_bind_message_plan.h"
#include "../tests/native_storage/reader_probe.h"
#include "tinytest.h"

#include <cmeta_thread.h>
#include <tstr.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
  PREPARED_TEST_THREADS = 4u,
  PREPARED_TEST_REPEATS = 40u,
  PREPARED_TEST_WORKSPACE_BYTES = 4096u,
  PREPARED_TEST_MAX_DEPTH = 8u,
  PREPARED_TEST_MAX_ITEMS = 64u
};
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
  while (!atomic_load_explicit(worker->start, memory_order_acquire)) cmeta_thread_yield();
  worker->status = data_bind_message_plan_acquire_generated(
      worker->codec, &COUNTED_ARTIFACT, &worker->plan, NULL);
}

static cserde_status counted_writer_write(void *context, const cserde_token *token) {
  if (context == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  ++*(size_t *)context;
  return CSERDE_OK;
}

static cserde_status counted_writer_finish(void *context) {
  if (context == NULL) return CSERDE_INVALID_ARGUMENT;
  ++*(size_t *)context;
  return CSERDE_OK;
}

static const cserde_writer_ops COUNTED_WRITER_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    counted_writer_write, counted_writer_finish};

static void require_message_diagnostic_rejection(size_t size, uint32_t abi_version) {
  static const uint8_t source_text[] = {'n', 'e', 'w'};
  static const uint8_t field_name[] = "text";
  DataBindNativeTypeBinding binding = DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
  DataBindMessagePlan *owned = NULL;
  DataBindMessagePlan *output = NULL;
  DataBindMessagePlanDiagnostic diagnostic = DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
  unsigned char diagnostic_before[sizeof(diagnostic)];
  unsigned char workspace[PREPARED_TEST_WORKSPACE_BYTES] = {0};
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  NativeReaderProbe probe = {0};
  cserde_reader reader = {0};
  cserde_writer writer = {0};
  size_t writer_calls = 0u;
  const NativeReaderProbeStep steps[] = {
      native_reader_probe_token(CSERDE_MAP_BEGIN),
      native_reader_probe_slice(CSERDE_STRING, field_name, sizeof(field_name) - 1u, CSERDE_VIEW_STABLE),
      native_reader_probe_slice(CSERDE_STRING, source_text, sizeof(source_text), CSERDE_VIEW_STABLE),
      native_reader_probe_token(CSERDE_MAP_END)};
  TailOnly_t value;

  check_equal(TailOnly_native_artifact()->native_binding(&binding, &error), DATA_BIND_OK);
  check_equal(data_bind_message_plan_compile(
      codec, "TailOnly", &binding, &owned, &diagnostic), DATA_BIND_OK);
  check_not_null(owned);
  TailOnly_init(&value);
  value.text = tstr_dup("retained");
  check_not_null(value.text);
  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = PREPARED_TEST_MAX_DEPTH;
  options.max_items = PREPARED_TEST_MAX_ITEMS;
  options.max_owned_bytes = sizeof(workspace);
  check_equal(native_reader_probe_open(&probe, steps, sizeof(steps) / sizeof(steps[0]),
                                      &reader), CSERDE_OK);
  check_equal(cserde_writer_init(&writer, &COUNTED_WRITER_OPS, &writer_calls), CSERDE_OK);
  diagnostic.size = size;
  diagnostic.abi_version = abi_version;
  memcpy(diagnostic_before, &diagnostic, sizeof(diagnostic));
  check_equal(data_bind_message_plan_compile(
      codec, "TailOnly", &binding, &output, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
  check_null(output);
  check_equal(data_bind_message_plan_compile_object(
      codec, "TailOnly", binding.data, &output, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
  check_null(output);
  check_equal(data_bind_message_plan_decode_native(
      owned, &options, &reader, &value, sizeof(value), &diagnostic), DATA_BIND_ERR_INVALID_ARG);
  check_equal(data_bind_message_plan_encode_native(
      owned, &options, &value, sizeof(value), &writer, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
  check_equal(value.text, "retained");
  check_equal(probe.calls, (size_t)0u);
  check_equal(writer_calls, (size_t)0u);
  check_equal(memcmp(&diagnostic, diagnostic_before, sizeof(diagnostic)), 0);
  TailOnly_clear(&value);
  data_bind_message_plan_free(owned);
}

spec("Generated codec-owned MessagePlan preparation") {
  before_each() {
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    atomic_store(&resolver_calls, 0u);
    atomic_store(&reject_resolver, 0);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
  }
  after_each() { data_bind_free(codec); codec = NULL; }


  it("round-trips generated Bool8 fields including present false") {
    static const char input[] = "{\"enabled\":true,\"flag\":false}";
    BooleanWire_t value, roundtrip;
    char *text = NULL;
    size_t text_size = 0u;
    BooleanWire_init(&value);
    BooleanWire_init(&roundtrip);
    check_equal(BooleanWire_from_json(codec, &value, input, sizeof(input) - 1u,
        &error), DATA_BIND_OK);
    check_equal(value.enabled, 1u);
    check_equal(value.flag, 0u);
    check_equal(value._presence[0] & 1u, 1u);
    check_equal(BooleanWire_to_json(codec, &value, &text, &text_size,
        &error), DATA_BIND_OK);
    check_equal(BooleanWire_from_json(codec, &roundtrip, text, text_size,
        &error), DATA_BIND_OK);
    check_equal(roundtrip.enabled, value.enabled);
    check_equal(roundtrip.flag, value.flag);
    check_equal(roundtrip._presence[0], value._presence[0]);
    data_bind_serialized_free(text);
    BooleanWire_clear(&roundtrip);
    BooleanWire_clear(&value);
  }

  it("preserves absent optional Bool8 and rejects integer Boolean input") {
    static const char absent[] = "{\"enabled\":false}";
    static const char wrong[] = "{\"enabled\":1,\"flag\":false}";
    BooleanWire_t value;
    BooleanWire_init(&value);
    check_equal(BooleanWire_from_json(codec, &value, absent, sizeof(absent) - 1u,
        &error), DATA_BIND_OK);
    check_equal(value.enabled, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(BooleanWire_from_json(codec, &value, wrong, sizeof(wrong) - 1u,
        &error), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(value.enabled, 0u);
    check_equal(value._presence[0], 0u);
    BooleanWire_clear(&value);
  }

  it("publishes one complete native plan to concurrent cold callers") {
    PrepareWorker workers[PREPARED_TEST_THREADS] = {0};
    cmeta_thread_t threads[PREPARED_TEST_THREADS] = {0};
    atomic_int start = 0;
    size_t created = 0u;
    const DataBindMessagePlan *warm = NULL;
    size_t cold_calls;
    for (; created < PREPARED_TEST_THREADS; ++created) {
      workers[created].codec = codec;
      workers[created].start = &start;
      if (cmeta_thread_create(&threads[created], prepare_worker, &workers[created]) != 0)
        break;
    }
    atomic_store_explicit(&start, 1, memory_order_release);
    for (size_t i = 0u; i < created; ++i)
      check_equal(cmeta_thread_join(&threads[i]), 0);
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
    artifact.abi_version = DATA_BIND_MESSAGE_NATIVE_ARTIFACT_ABI_VERSION - 1u;
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifact, &plan, &error), DATA_BIND_ERR_INVALID_ARG);
    check_null(plan);
    artifact.abi_version = DATA_BIND_MESSAGE_NATIVE_ARTIFACT_ABI_VERSION;
    artifact.size = sizeof(artifact) + 1u;
    check_false(data_bind_message_native_artifact_valid(&artifact));
    check_equal(data_bind_message_plan_acquire_generated(
        codec, &artifact, &plan, &error), DATA_BIND_ERR_INVALID_ARG);
    check_null(plan);
    check_equal(atomic_load(&resolver_calls), (size_t)0u);
  }

  it("rejects an old same-size MessagePlan diagnostic ABI without rewriting it") {
    require_message_diagnostic_rejection(
        sizeof(DataBindMessagePlanDiagnostic), DATA_BIND_MESSAGE_PLAN_ABI_VERSION - 1u);
  }
  it("rejects a short MessagePlan diagnostic instead of publishing partial fields") {
    require_message_diagnostic_rejection(
        offsetof(DataBindMessagePlanDiagnostic, schema_field), DATA_BIND_MESSAGE_PLAN_ABI_VERSION);
  }
  it("rejects an extended MessagePlan diagnostic instead of interpreting its prefix") {
    require_message_diagnostic_rejection(
        sizeof(DataBindMessagePlanDiagnostic) + 1u, DATA_BIND_MESSAGE_PLAN_ABI_VERSION);
  }

  it("rejects extended native binding records instead of copying their current prefix") {
    DataBindNativeTypeBinding binding = DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL);
    DataBindMessagePlan *plan = NULL;
    DataBindMessagePlanDiagnostic diagnostic = DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_equal(TailOnly_native_artifact()->native_binding(&binding, &error), DATA_BIND_OK);
    binding.size += 1u;
    check_equal(data_bind_message_plan_compile(
        codec, "TailOnly", &binding, &plan, &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_equal(diagnostic.status, DATA_BIND_ERR_SCHEMA);
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

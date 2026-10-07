#include "nested_native_generics_native.h"
#include "data_bind_format_provider.h"
#include "data_bind_message_plan.h"
#include "data_bind_native.h"
#include "data_bind_xml_writer.h"
#include "data_bind_projection_plan.h"
#include <cmeta/cleanup.h>
#include <cmeta/fixed_array.h>
#include <cmeta_thread.h>
#include "tinytest.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum cleanup_fault {
  CLEANUP_NO_FAULT,
  CLEANUP_STAGING_OOM,
  CLEANUP_PROBE_OOM,
  CLEANUP_WORKSPACE_OOM,
  CLEANUP_FORMAT_PLAN_FAILURE,
  CLEANUP_READER_OPEN_FAILURE,
  CLEANUP_CANONICAL_READER_FAILURE,
  CLEANUP_CANONICAL_AND_CLOSE_FAILURE,
  CLEANUP_READER_CLOSE_FAILURE,
  CLEANUP_MOVE_FAILURE
};
enum cleanup_event {
  CLEANUP_READER_CLOSED,
  CLEANUP_TEMPORARY_RESTORED,
  CLEANUP_FORMAT_PLAN_FREED,
  CLEANUP_WORKSPACE_FREED,
  CLEANUP_STAGING_FREED
};
enum {
  CLEANUP_STAGING_SLOT,
  CLEANUP_PROBE_SLOT,
  CLEANUP_WORKSPACE_SLOT,
  CLEANUP_ALLOCATION_COUNT,
  CLEANUP_EVENT_CAPACITY = 16,
  CLEANUP_PREVIOUS_ID = 19,
  CLEANUP_DECODED_ID = 7,
  CLEANUP_DESCRIPTOR_DEPTH = 8,
  CLEANUP_DESCRIPTOR_NODES = 8
};
static const char cleanup_previous_name[] = "previous";
static const char cleanup_published_name[] = "owned";
static struct {
  int active;
  enum cleanup_fault fault;
  void *allocations[CLEANUP_ALLOCATION_COUNT];
  size_t allocation_calls;
  size_t allocation_frees[CLEANUP_ALLOCATION_COUNT];
  enum cleanup_event events[CLEANUP_EVENT_CAPACITY];
  size_t event_count;
  size_t reader_opens;
  size_t reader_closes;
  size_t format_plans;
  size_t format_frees;
  size_t temporary_restores;
  size_t moves;
  void *temporary;
  int decode_rolled_back;
} cleanup_observation;

static void cleanup_record(enum cleanup_event event) {
  check_less(cleanup_observation.event_count, (size_t)CLEANUP_EVENT_CAPACITY);
  cleanup_observation.events[cleanup_observation.event_count++] = event;
}

static void *cleanup_malloc(size_t bytes) {
  void *allocation;
  size_t index;
  if (!cleanup_observation.active) return malloc(bytes);
  index = cleanup_observation.allocation_calls++;
  check_less(index, (size_t)CLEANUP_ALLOCATION_COUNT);
  if ((index == CLEANUP_STAGING_SLOT && cleanup_observation.fault == CLEANUP_STAGING_OOM) ||
      (index == CLEANUP_PROBE_SLOT && cleanup_observation.fault == CLEANUP_PROBE_OOM) ||
      (index == CLEANUP_WORKSPACE_SLOT && cleanup_observation.fault == CLEANUP_WORKSPACE_OOM))
    return NULL;
  allocation = malloc(bytes);
  cleanup_observation.allocations[index] = allocation;
  return allocation;
}

static void cleanup_free(void *allocation) {
  size_t index;
  if (cleanup_observation.active && allocation != NULL) {
    for (index = 0u; index < CLEANUP_ALLOCATION_COUNT; ++index) {
      if (allocation != cleanup_observation.allocations[index]) continue;
      ++cleanup_observation.allocation_frees[index];
      cleanup_observation.allocations[index] = NULL;
      if (index == CLEANUP_STAGING_SLOT) cleanup_record(CLEANUP_STAGING_FREED);
      if (index == CLEANUP_WORKSPACE_SLOT) cleanup_record(CLEANUP_WORKSPACE_FREED);
      break;
    }
    check_less(index, (size_t)CLEANUP_ALLOCATION_COUNT);
  }
  free(allocation);
}

static DataBindStatus cleanup_format_compile(
    DataBind *codec, const char *type_name, DataBindFormat format,
    DataBindFormatPlan **out, DataBindError *error) {
  DataBindStatus status;
  if (cleanup_observation.active && cleanup_observation.fault == CLEANUP_FORMAT_PLAN_FAILURE)
    return DATA_BIND_ERR_OOM;
  status = data_bind_format_plan_compile(codec, type_name, format, out, error);
  if (cleanup_observation.active && status == DATA_BIND_OK) ++cleanup_observation.format_plans;
  return status;
}

static DataBindStatus cleanup_format_compile_reader(
    DataBind *codec, const char *type_name, DataBindFormat format,
    DataBindFormatPlan **out, DataBindError *error) {
  DataBindStatus status = data_bind_format_plan_compile_reader(codec, type_name, format, out, error);
  if (cleanup_observation.active && status == DATA_BIND_OK) ++cleanup_observation.format_plans;
  return status;
}

static void cleanup_format_free(DataBindFormatPlan *plan) {
  if (cleanup_observation.active) {
    check_not_null(plan);
    ++cleanup_observation.format_frees;
    cleanup_record(CLEANUP_FORMAT_PLAN_FREED);
  }
  data_bind_format_plan_free(plan);
}

static DataBindStatus cleanup_reader_open(
    const DataBindFormatProvider *provider, const char *data, size_t len,
    size_t depth, DataBindFormatReader *out, DataBindError *error) {
  DataBindStatus status;
  if (cleanup_observation.active && cleanup_observation.fault == CLEANUP_READER_OPEN_FAILURE)
    return DATA_BIND_ERR_PARSE;
  status = data_bind_format_reader_open(provider, data, len, depth, out, error);
  if (cleanup_observation.active && status == DATA_BIND_OK) ++cleanup_observation.reader_opens;
  return status;
}

static DataBindStatus cleanup_reader_close(DataBindFormatReader *reader) {
  DataBindStatus status;
  if (cleanup_observation.active) {
    check_not_null(reader->provider);
    ++cleanup_observation.reader_closes;
    cleanup_record(CLEANUP_READER_CLOSED);
  }
  status = data_bind_format_reader_close(reader);
  if (cleanup_observation.active &&
      (cleanup_observation.fault == CLEANUP_READER_CLOSE_FAILURE ||
       cleanup_observation.fault == CLEANUP_CANONICAL_AND_CLOSE_FAILURE))
    return DATA_BIND_ERR_PARSE;
  return status;
}

static DataBindStatus cleanup_canonical_reader_init(
    const DataBindFormatPlan *plan, cserde_reader *reader,
    DataBindFormatCanonicalReader *out, DataBindError *error) {
  if (cleanup_observation.active &&
      (cleanup_observation.fault == CLEANUP_CANONICAL_READER_FAILURE ||
       cleanup_observation.fault == CLEANUP_CANONICAL_AND_CLOSE_FAILURE))
    return DATA_BIND_ERR_RUNTIME;
  return data_bind_format_canonical_reader_init(plan, reader, out, error);
}

static DataBindStatus cleanup_decode(
    const DataBindMessagePlan *plan, const DataBindNativeOptions *options,
    DataBindFormat format, cserde_reader *reader, void *temporary,
    size_t bytes, DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindStatus status = data_bind_message_plan_decode_native_format(
      plan, options, format, reader, temporary, bytes, diagnostic);
  if (cleanup_observation.active) {
    const User_t *user = temporary;
    cleanup_observation.temporary = temporary;
    if (status != DATA_BIND_OK)
      cleanup_observation.decode_rolled_back = user->name == NULL && user->id == 0;
  }
  return status;
}

static cmeta_status cleanup_restore(const cmeta_data_desc *data, void *object) {
  if (cleanup_observation.active && object == cleanup_observation.temporary) {
    ++cleanup_observation.temporary_restores;
    cleanup_record(CLEANUP_TEMPORARY_RESTORED);
  }
  return cmeta_data_value_restore_zero(data, object);
}

static cmeta_status cleanup_move(const cmeta_data_desc *data, void *destination, void *source) {
  if (cleanup_observation.active) {
    ++cleanup_observation.moves;
    if (cleanup_observation.fault == CLEANUP_MOVE_FAILURE) return CMETA_OUT_OF_MEMORY;
  }
  return cmeta_data_value_move(data, destination, source);
}

/* Interpose only generated calls in this test TU, delegating to real domain
 * APIs. Production declarations and generation have no test hooks; rollback
 * still executes inside the real MessagePlan runtime. */
#define malloc cleanup_malloc
#define free cleanup_free
#define data_bind_format_plan_compile cleanup_format_compile
#define data_bind_format_plan_compile_reader cleanup_format_compile_reader
#define data_bind_format_plan_free cleanup_format_free
#define data_bind_format_reader_open cleanup_reader_open
#define data_bind_format_reader_close cleanup_reader_close
#define data_bind_format_canonical_reader_init cleanup_canonical_reader_init
#define data_bind_message_plan_decode_native_format cleanup_decode
#define cmeta_data_value_restore_zero cleanup_restore
#define cmeta_data_value_move cleanup_move
#include "nested_native_generics_native.c"
#undef cmeta_data_value_move
#undef cmeta_data_value_restore_zero
#undef data_bind_message_plan_decode_native_format
#undef data_bind_format_canonical_reader_init
#undef data_bind_format_reader_close
#undef data_bind_format_reader_open
#undef data_bind_format_plan_free
#undef data_bind_format_plan_compile_reader
#undef data_bind_format_plan_compile
#undef free
#undef malloc

static void cleanup_check_events(const enum cleanup_event *expected, size_t count) {
  check_equal(cleanup_observation.event_count, count);
  check_equal(cleanup_observation.events, expected, count * sizeof(*expected));
}

spec("generated native decode cleanup obligations") {
  static DataBind *codec;
  static User_t destination;
  static DataBindError error;
  static const char input[] = "{\"id\":7,\"name\":\"owned\"}";

  before_each() {
    const DataBindMessagePlan *borrowed = NULL;
    memset(&cleanup_observation, 0, sizeof(cleanup_observation));
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    codec = NULL;
    check_equal(NestedNative_codec_create(&codec, &error), DATA_BIND_OK);
    User_init(&destination);
    destination.id = CLEANUP_PREVIOUS_ID;
    destination.name = tstr_dup(cleanup_previous_name);
    check_not_null(destination.name);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, User_native_artifact(), &borrowed, &error), DATA_BIND_OK);
    cleanup_observation.active = 1;
  }

  after_each() {
    size_t index;
    cleanup_observation.active = 0;
    for (index = 0u; index < CLEANUP_ALLOCATION_COUNT; ++index)
      check_null(cleanup_observation.allocations[index]);
    User_clear(&destination);
    data_bind_free(codec);
  }

  it("retains the codec-owned plan on acquisition failure without acquiring local resources") {
    DataBindMessageNativeArtifact invalid = *User_native_artifact();
    const DataBindMessagePlan *first = NULL;
    const DataBindMessagePlan *again = NULL;
    invalid.type_name = "Missing";
    check_not_equal(NestedNative_message_from_text(codec, &invalid,
        data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON),
        CLEANUP_DESCRIPTOR_DEPTH, CLEANUP_DESCRIPTOR_NODES, 0u,
        &destination, sizeof(destination), input, sizeof(input) - 1u, &error), DATA_BIND_OK);
    check_equal(cleanup_observation.allocation_calls, (size_t)0u);
    check_equal(destination.id, CLEANUP_PREVIOUS_ID);
    check_equal(destination.name, cleanup_previous_name);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, User_native_artifact(), &first, &error), DATA_BIND_OK);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, User_native_artifact(), &again, &error), DATA_BIND_OK);
    check_true(first == again);
  }

  group("failure before native value publication") {
    static const struct {
      const char *name;
      enum cleanup_fault fault;
      DataBindStatus status;
      enum cleanup_event events[CLEANUP_EVENT_CAPACITY];
      size_t event_count;
      size_t reader_closes;
      size_t value_restores;
    } cases[] = {
      {"staging allocation failure", CLEANUP_STAGING_OOM, DATA_BIND_ERR_OOM, {0}, 0u, 0u, 0u},
      {"workspace probe allocation failure", CLEANUP_PROBE_OOM, DATA_BIND_ERR_OOM,
       {CLEANUP_STAGING_FREED}, 1u, 0u, 0u},
      {"workspace allocation failure", CLEANUP_WORKSPACE_OOM, DATA_BIND_ERR_OOM,
       {CLEANUP_STAGING_FREED}, 1u, 0u, 0u},
      {"format plan failure", CLEANUP_FORMAT_PLAN_FAILURE, DATA_BIND_ERR_OOM,
       {CLEANUP_WORKSPACE_FREED, CLEANUP_STAGING_FREED}, 2u, 0u, 0u},
      {"reader open failure", CLEANUP_READER_OPEN_FAILURE, DATA_BIND_ERR_PARSE,
       {CLEANUP_FORMAT_PLAN_FREED, CLEANUP_WORKSPACE_FREED, CLEANUP_STAGING_FREED}, 3u, 0u, 0u},
      {"canonical reader failure", CLEANUP_CANONICAL_READER_FAILURE, DATA_BIND_ERR_RUNTIME,
       {CLEANUP_READER_CLOSED, CLEANUP_FORMAT_PLAN_FREED, CLEANUP_WORKSPACE_FREED,
        CLEANUP_STAGING_FREED}, 4u, 1u, 0u},
      {"canonical reader failure followed by close failure", CLEANUP_CANONICAL_AND_CLOSE_FAILURE,
       DATA_BIND_ERR_RUNTIME,
       {CLEANUP_READER_CLOSED, CLEANUP_FORMAT_PLAN_FREED, CLEANUP_WORKSPACE_FREED,
        CLEANUP_STAGING_FREED}, 4u, 1u, 0u},
      {"reader close failure", CLEANUP_READER_CLOSE_FAILURE, DATA_BIND_ERR_PARSE,
       {CLEANUP_READER_CLOSED, CLEANUP_TEMPORARY_RESTORED, CLEANUP_FORMAT_PLAN_FREED,
        CLEANUP_WORKSPACE_FREED, CLEANUP_STAGING_FREED}, 5u, 1u, 1u},
      {"publication move failure", CLEANUP_MOVE_FAILURE, DATA_BIND_ERR_RUNTIME,
       {CLEANUP_READER_CLOSED, CLEANUP_TEMPORARY_RESTORED, CLEANUP_FORMAT_PLAN_FREED,
        CLEANUP_WORKSPACE_FREED, CLEANUP_STAGING_FREED}, 5u, 1u, 1u}
    };
    size_t index;
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
      it("%s releases only live obligations in reverse order", cases[index].name) {
        cleanup_observation.fault = cases[index].fault;
        check_equal(User_from_json(codec, &destination, input, sizeof(input) - 1u, &error),
                    cases[index].status);
        cleanup_check_events(cases[index].events, cases[index].event_count);
        check_equal(cleanup_observation.reader_closes, cases[index].reader_closes);
        check_equal(cleanup_observation.temporary_restores, cases[index].value_restores);
        check_equal(cleanup_observation.format_frees, cleanup_observation.format_plans);
        if (cases[index].fault == CLEANUP_MOVE_FAILURE) {
          check_equal(destination.id, 0);
          check_null(destination.name);
        } else {
          check_equal(destination.id, CLEANUP_PREVIOUS_ID);
          check_equal(destination.name, cleanup_previous_name);
        }
      }
    }
  }

  it("leaves failed decode rollback with MessagePlan and preserves the old owner") {
    static const char invalid[] = "{\"id\":7,\"name\":\"owned\",\"unknown\":1}";
    static const enum cleanup_event expected[] = {
      CLEANUP_READER_CLOSED, CLEANUP_FORMAT_PLAN_FREED,
      CLEANUP_WORKSPACE_FREED, CLEANUP_STAGING_FREED};
    check_equal(User_from_json(codec, &destination, invalid, sizeof(invalid) - 1u, &error),
                DATA_BIND_ERR_SCHEMA);
    check_true(cleanup_observation.decode_rolled_back);
    check_equal(cleanup_observation.temporary_restores, (size_t)0u);
    check_equal(cleanup_observation.moves, (size_t)0u);
    cleanup_check_events(expected, sizeof(expected) / sizeof(expected[0]));
    check_equal(destination.id, CLEANUP_PREVIOUS_ID);
    check_equal(destination.name, cleanup_previous_name);
  }

  it("lets MessagePlan roll back an owned field when a later required field is absent") {
    static const char invalid[] = "{\"name\":\"owned\"}";
    static const enum cleanup_event expected[] = {
      CLEANUP_READER_CLOSED, CLEANUP_FORMAT_PLAN_FREED,
      CLEANUP_WORKSPACE_FREED, CLEANUP_STAGING_FREED};
    check_equal(User_from_json(codec, &destination, invalid, sizeof(invalid) - 1u, &error),
                DATA_BIND_ERR_TYPE_NOT_FOUND);
    check_true(cleanup_observation.decode_rolled_back);
    check_equal(cleanup_observation.temporary_restores, (size_t)0u);
    check_equal(cleanup_observation.moves, (size_t)0u);
    cleanup_check_events(expected, sizeof(expected) / sizeof(expected[0]));
    check_equal(destination.id, CLEANUP_PREVIOUS_ID);
    check_equal(destination.name, cleanup_previous_name);
  }

  group("format-specific acquisition paths") {
    static const struct {
      const char *name;
      DataBindFormat format;
      const char *input;
    } cases[] = {
      {"XML", DATA_BIND_FORMAT_XML, "<User><id>7</id><name>owned</name></User>"},
      {"YAML", DATA_BIND_FORMAT_YAML, "id: 7\nname: owned\n"},
      {"CSV", DATA_BIND_FORMAT_CSV, "id,name\n7,owned\n"}
    };
    size_t index;
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
      it("%s consumes its domain reader and plan once before releasing workspace", cases[index].name) {
        static const enum cleanup_event expected[] = {
          CLEANUP_READER_CLOSED, CLEANUP_FORMAT_PLAN_FREED,
          CLEANUP_WORKSPACE_FREED, CLEANUP_STAGING_FREED};
        check_equal(NestedNative_message_from_text(codec, User_native_artifact(),
            data_bind_builtin_format_provider(cases[index].format),
            CLEANUP_DESCRIPTOR_DEPTH, CLEANUP_DESCRIPTOR_NODES, 0u,
            &destination, sizeof(destination), cases[index].input,
            strlen(cases[index].input), &error), DATA_BIND_OK);
        cleanup_check_events(expected, sizeof(expected) / sizeof(expected[0]));
        check_equal(cleanup_observation.format_plans, (size_t)1u);
        check_equal(cleanup_observation.format_frees, (size_t)1u);
        check_equal(cleanup_observation.reader_closes, (size_t)1u);
        check_equal(cleanup_observation.temporary_restores, (size_t)0u);
        check_equal(destination.id, CLEANUP_DECODED_ID);
        check_equal(destination.name, cleanup_published_name);
      }
    }
  }

  it("publishes owned storage without restoring the moved source and reuses the borrowed plan") {
    static const enum cleanup_event expected[] = {
      CLEANUP_READER_CLOSED, CLEANUP_FORMAT_PLAN_FREED,
      CLEANUP_WORKSPACE_FREED, CLEANUP_STAGING_FREED};
    const DataBindMessagePlan *before = NULL;
    const DataBindMessagePlan *after = NULL;
    check_equal(data_bind_message_plan_acquire_generated(
        codec, User_native_artifact(), &before, &error), DATA_BIND_OK);
    check_equal(User_from_json(codec, &destination, input, sizeof(input) - 1u, &error), DATA_BIND_OK);
    check_equal(cleanup_observation.moves, (size_t)1u);
    check_equal(cleanup_observation.temporary_restores, (size_t)0u);
    check_equal(cleanup_observation.reader_opens, (size_t)1u);
    check_equal(cleanup_observation.reader_closes, (size_t)1u);
    check_equal(cleanup_observation.format_plans, (size_t)1u);
    check_equal(cleanup_observation.format_frees, (size_t)1u);
    check_equal(cleanup_observation.allocation_frees[CLEANUP_STAGING_SLOT], (size_t)1u);
    check_equal(cleanup_observation.allocation_frees[CLEANUP_PROBE_SLOT], (size_t)1u);
    check_equal(cleanup_observation.allocation_frees[CLEANUP_WORKSPACE_SLOT], (size_t)1u);
    cleanup_check_events(expected, sizeof(expected) / sizeof(expected[0]));
    check_equal(destination.id, CLEANUP_DECODED_ID);
    check_equal(destination.name, cleanup_published_name);
    check_equal(data_bind_message_plan_acquire_generated(
        codec, User_native_artifact(), &after, &error), DATA_BIND_OK);
    check_true(before == after);
  }
}

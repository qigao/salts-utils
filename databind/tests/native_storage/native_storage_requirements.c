/* Canonical native storage admission. */
#include "data_bind_native.h"
#include <cmeta_cmeta_data.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct ProbeInt { int value; } ProbeInt;
typedef struct ProbeLong { long value; } ProbeLong;
typedef struct ProbeBool { bool value; } ProbeBool;
typedef struct ProbeBool8 { uint8_t value; } ProbeBool8;
typedef struct ProbeI32 { int32_t value; } ProbeI32;
typedef struct ProbeU64 { uint64_t value; } ProbeU64;
typedef struct ProbeDouble { double value; } ProbeDouble;
typedef struct ProbeBuffer { tstr value; } ProbeBuffer;

typedef union ProbeStorage {
  ProbeInt native_int;
  ProbeLong native_long;
  ProbeBool native_bool;
  ProbeBool8 bool8;
  ProbeI32 i32;
  ProbeU64 u64;
  ProbeDouble real;
  ProbeBuffer buffer;
} ProbeStorage;

typedef struct ProbeDescriptor {
  cmeta_type_identity identity;
  cmeta_type_desc type;
  cmeta_field_desc layout_field;
  cmeta_struct_desc layout;
  cmeta_data_field_desc field;
  cmeta_data_struct_shape shape;
  cmeta_data_desc data;
} ProbeDescriptor;

enum { PROBE_WORKSPACE_BYTES = 4096, PROBE_MAX_DEPTH = 8,
       PROBE_MAX_ITEMS = 32, PROBE_POISON = 0xa5 };
static _Alignas(64) unsigned char probe_workspace[PROBE_WORKSPACE_BYTES];
static DataBindNativeOptions probe_options;
static DataBindNativeDiagnostic probe_diagnostic;
static ProbeStorage storage;
static const cmeta_data_buffer_ops *cleanup_buffer;

static void reset_native(void) {
  probe_options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
  probe_diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  probe_options.workspace = probe_workspace;
  probe_options.workspace_bytes = sizeof(probe_workspace);
  probe_options.max_depth = PROBE_MAX_DEPTH;
  probe_options.max_items = PROBE_MAX_ITEMS;
}

static void probe_describe(ProbeDescriptor *probe, const char *name,
                     const cmeta_data_desc *leaf, size_t size, size_t alignment,
                     size_t field_offset, size_t field_size,
                     size_t field_alignment) {
  memset(probe, 0, sizeof(*probe));
  probe->identity = (cmeta_type_identity)CMETA_TYPE_ID_ATOM_INIT(name);
  probe->type = (cmeta_type_desc){
      .name = name, .size = size, .align = alignment,
      .kind = CMETA_T_OBJECT, .identity = &probe->identity};
  probe->layout_field = (cmeta_field_desc){
      .name = "value", .type_name = leaf->storage_type->name,
      .offset = field_offset, .size = field_size, .align = field_alignment,
      .type = leaf->storage_type};
  probe->layout = (cmeta_struct_desc){
      name, size, alignment, &probe->layout_field, 1u};
  probe->field = (cmeta_data_field_desc){
      "test.native-reader-requirements.value", "value", field_offset, leaf};
  probe->shape = (cmeta_data_struct_shape){&probe->layout, &probe->field, 1u};
  probe->data = (cmeta_data_desc){
      .struct_size = sizeof(cmeta_data_desc),
      .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = name, .display_name = name, .kind = CMETA_DATA_STRUCT,
      .storage_type = &probe->type, .shape = &probe->shape};
}

#define DESCRIBE(PROBE, ROW, FIELD_TYPE, LEAF) \
  probe_describe(&(PROBE), #ROW, (LEAF), sizeof(ROW), _Alignof(ROW), \
           offsetof(ROW, value), sizeof(((ROW *)0)->value), \
           _Alignof(FIELD_TYPE))

static void require_storage(ProbeDescriptor *probe) {
  DataBindNativeRequirements requirements = DATA_BIND_NATIVE_REQUIREMENTS_INIT;
  check_true(cmeta_data_desc_valid(probe->field.value));
  check_true(cmeta_data_desc_valid(&probe->data));
  const DataBindStatus status = data_bind_native_measure(
      &probe_options, &probe->data, &requirements, &probe_diagnostic);
  (void)printf("NATIVE_REQUIREMENT name=%s status=%d path=%s message=%s\n",
                probe->type.name, (int)status,
                probe_diagnostic.error.path, probe_diagnostic.error.message);
  check_equal(status, DATA_BIND_OK);
  check_equal(data_bind_native_init(&probe_options, &probe->data, &storage,
                                    sizeof(storage), &probe_diagnostic), DATA_BIND_OK);
  check_equal(data_bind_native_clear(&probe_options, &probe->data, &storage,
                                     sizeof(storage), &probe_diagnostic), DATA_BIND_OK);
}

static void require_owned_buffer(cmeta_data_kind kind) {
  ProbeDescriptor probe;
  cmeta_data_desc leaf = cmeta_tstr_cmeta_data;
  leaf.kind = kind;
  leaf.stable_id = kind == CMETA_DATA_STRING ? "test.reader.owned-text" : "test.reader.owned-bytes";
  leaf.display_name = leaf.stable_id;
  DESCRIBE(probe, ProbeBuffer, tstr, &leaf);
  require_storage(&probe);

  check_equal(data_bind_native_init(&probe_options, &probe.data, &storage,
                                    sizeof(storage), &probe_diagnostic), DATA_BIND_OK);
  cleanup_buffer = leaf.buffer_ops;
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
  static const unsigned char payload[] = {0u, 'A', 'B'};
  check_equal(cleanup_buffer->assign(&storage.buffer.value, payload, sizeof(payload), sizeof(payload)),
              CMETA_OK);
  check_false(cleanup_buffer->is_zero(&storage.buffer.value));
  check_equal(data_bind_native_clear(&probe_options, &probe.data, &storage,
                                     sizeof(storage), &probe_diagnostic), DATA_BIND_OK);
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
  /* A second clear must not free the same owning value twice. */
  check_equal(data_bind_native_clear(&probe_options, &probe.data, &storage,
                                     sizeof(storage), &probe_diagnostic), DATA_BIND_OK);
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
}

static void reject_without_touching(ProbeDescriptor *probe) {
  DataBindNativeRequirements requirements = DATA_BIND_NATIVE_REQUIREMENTS_INIT;
  unsigned char before[sizeof(storage)];
  memset(&storage, PROBE_POISON, sizeof(storage));
  memcpy(before, &storage, sizeof(storage));
  check_equal(data_bind_native_measure(&probe_options, &probe->data,
              &requirements, &probe_diagnostic), DATA_BIND_ERR_SCHEMA);
  check_equal(data_bind_native_init(&probe_options, &probe->data, &storage,
                                    sizeof(storage), &probe_diagnostic), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(before, &storage, sizeof(storage)), 0);
  check_equal(data_bind_native_clear(&probe_options, &probe->data, &storage,
                                     sizeof(storage), &probe_diagnostic), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(before, &storage, sizeof(storage)), 0);
  check_equal(probe_diagnostic.error.code, DATA_BIND_ERR_SCHEMA);
  check_true(probe_diagnostic.error.message[0] != '\0');
}

spec("DataBind canonical native storage admission") {
  before_each() {
    reset_native();
    memset(&storage, 0, sizeof(storage));
    cleanup_buffer = NULL;
  }
  after_each() {
    /* Only a successfully initialized concrete buffer reaches this cleanup.
     * This releases fixture-owned payload, never supplies a production hold. */
    if (cleanup_buffer != NULL) cleanup_buffer->restore_zero(&storage.buffer.value);
    cleanup_buffer = NULL;
  }

  it("preserves canonical fixed-width signed storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeI32, int32_t, &cmeta_data_int32);
    require_storage(&probe);
  }
  it("preserves canonical fixed-width unsigned storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeU64, uint64_t, &cmeta_data_uint64);
    require_storage(&probe);
  }
  it("preserves the existing explicit bool8 provider") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeBool8, uint8_t, &cmeta_bool8_cmeta_data);
    require_storage(&probe);
  }
  it("preserves canonical double storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeDouble, double, &cmeta_data_double);
    require_storage(&probe);
  }
  it("accepts native C long independently of the platform width") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeLong, long, &cmeta_data_long);
    require_storage(&probe);
  }
  it("accepts C bool without substituting bool8 storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeBool, bool, &cmeta_data_bool);
    require_storage(&probe);
  }
  it("initializes and clears canonical owned text through its buffer provider") {
    require_owned_buffer(CMETA_DATA_STRING);
  }
  it("initializes and clears canonical owned bytes through its buffer provider") {
    require_owned_buffer(CMETA_DATA_BYTES);
  }
  it("rejects missing buffer operations without touching destination storage") {
    ProbeDescriptor probe;
    cmeta_data_desc leaf = cmeta_tstr_cmeta_data;
    leaf.buffer_ops = NULL;
    DESCRIBE(probe, ProbeBuffer, tstr, &leaf);
    reject_without_touching(&probe);
  }
  it("rejects a field offset mismatch without touching destination storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeI32, int32_t, &cmeta_data_int32);
    ++probe.field.offset;
    reject_without_touching(&probe);
  }
}

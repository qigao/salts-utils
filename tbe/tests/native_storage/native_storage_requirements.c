/* #99 prerequisite: exercise the real canonical DataBind storage boundary.
 * These are migration requirements, not a direct-reader decode implementation. */
#include "tbe_typed.h"
#include <salts_cmeta_data.h>
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
  TbeTypedField wire;
  TbeTypedType overlay;
  TbeTypedDescriptor descriptor;
} ProbeDescriptor;

static ProbeStorage storage;
static const cmeta_data_buffer_ops *cleanup_buffer;

static void describe(ProbeDescriptor *probe, const char *name,
                     const cmeta_data_desc *leaf, size_t size, size_t alignment,
                     size_t field_offset, size_t field_size,
                     size_t field_alignment, TbeTypedKind wire_kind) {
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
  probe->wire = (TbeTypedField){.name = "value", .wire_kind = wire_kind};
  probe->overlay = (TbeTypedType){
      .name = name, .size = size, .fields = &probe->wire, .field_count = 1u};
  probe->descriptor = (TbeTypedDescriptor)TBE_TYPED_DESCRIPTOR_INIT(
      &probe->overlay, &probe->data);
}

#define DESCRIBE(PROBE, ROW, FIELD_TYPE, LEAF, WIRE) \
  describe(&(PROBE), #ROW, (LEAF), sizeof(ROW), _Alignof(ROW), \
           offsetof(ROW, value), sizeof(((ROW *)0)->value), \
           _Alignof(FIELD_TYPE), (WIRE))

static void require_storage(ProbeDescriptor *probe) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  check_true(cmeta_data_desc_valid(probe->field.value));
  check_true(cmeta_data_desc_valid(&probe->data));
  const DataBindStatus status = tbe_typed_descriptor_validate(&probe->descriptor, &error);
  (void)printf("NATIVE_REQUIREMENT name=%s status=%d path=%s message=%s\n",
                probe->type.name, (int)status, error.path, error.message);
  check_equal(status, DATA_BIND_OK);
  check_equal(tbe_typed_descriptor_init(&probe->descriptor, &storage, &error), DATA_BIND_OK);
  check_equal(tbe_typed_descriptor_clear(&probe->descriptor, &storage, &error), DATA_BIND_OK);
}

static void require_owned_buffer(cmeta_data_kind kind) {
  ProbeDescriptor probe;
  cmeta_data_desc leaf = salts_tstr_cmeta_data;
  leaf.kind = kind;
  leaf.stable_id = kind == CMETA_DATA_STRING ? "test.reader.owned-text" : "test.reader.owned-bytes";
  leaf.display_name = leaf.stable_id;
  DESCRIBE(probe, ProbeBuffer, tstr, &leaf,
           kind == CMETA_DATA_STRING ? TBE_TYPED_STRING : TBE_TYPED_BYTES);
  require_storage(&probe);

  DataBindError error = DATA_BIND_ERROR_INIT;
  check_equal(tbe_typed_descriptor_init(&probe.descriptor, &storage, &error), DATA_BIND_OK);
  cleanup_buffer = leaf.buffer_ops;
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
  static const unsigned char payload[] = {0u, 'A', 'B'};
  check_equal(cleanup_buffer->assign(&storage.buffer.value, payload, sizeof(payload), sizeof(payload)),
              CMETA_OK);
  check_false(cleanup_buffer->is_zero(&storage.buffer.value));
  check_equal(tbe_typed_descriptor_clear(&probe.descriptor, &storage, &error), DATA_BIND_OK);
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
  /* A second clear must not free the same owning value twice. */
  check_equal(tbe_typed_descriptor_clear(&probe.descriptor, &storage, &error), DATA_BIND_OK);
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
}

static void reject_without_touching(ProbeDescriptor *probe) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  unsigned char before[sizeof(storage)];
  memset(&storage, 0xa5, sizeof(storage));
  memcpy(before, &storage, sizeof(storage));
  check_equal(tbe_typed_descriptor_validate(&probe->descriptor, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(tbe_typed_descriptor_init(&probe->descriptor, &storage, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(before, &storage, sizeof(storage)), 0);
  check_equal(tbe_typed_descriptor_clear(&probe->descriptor, &storage, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(before, &storage, sizeof(storage)), 0);
  check_not_null(strstr(error.path, "value"));
}

spec("DataBind native storage requirements before reader cutover") {
  before_each() {
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
    DESCRIBE(probe, ProbeI32, int32_t, &salts_int32_cmeta_data, TBE_TYPED_I32);
    require_storage(&probe);
  }
  it("preserves canonical fixed-width unsigned storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeU64, uint64_t, &salts_uint64_cmeta_data, TBE_TYPED_U64);
    require_storage(&probe);
  }
  it("preserves the existing explicit bool8 provider") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeBool8, uint8_t, &salts_bool8_cmeta_data, TBE_TYPED_BOOL);
    require_storage(&probe);
  }
  it("preserves canonical double storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeDouble, double, &cmeta_data_double, TBE_TYPED_F64);
    require_storage(&probe);
  }
  it("accepts native C int without replacing its semantic identity") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeInt, int, &cmeta_data_int, TBE_TYPED_I32);
    require_storage(&probe);
  }
  it("accepts native C long independently of the platform width") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeLong, long, &cmeta_data_long,
             sizeof(long) == sizeof(int32_t) ? TBE_TYPED_I32 : TBE_TYPED_I64);
    require_storage(&probe);
  }
  it("accepts C bool without substituting bool8 storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeBool, bool, &cmeta_data_bool, TBE_TYPED_BOOL);
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
    cmeta_data_desc leaf = salts_tstr_cmeta_data;
    leaf.buffer_ops = NULL;
    DESCRIBE(probe, ProbeBuffer, tstr, &leaf, TBE_TYPED_STRING);
    reject_without_touching(&probe);
  }
  it("rejects a field offset mismatch without touching destination storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeI32, int32_t, &salts_int32_cmeta_data, TBE_TYPED_I32);
    ++probe.field.offset;
    reject_without_touching(&probe);
  }
  it("takes native layout from CMeta rather than obsolete overlay offsets") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeI32, int32_t, &salts_int32_cmeta_data, TBE_TYPED_I32);
    probe.wire.offset = SIZE_MAX;
    probe.wire.kind = TBE_TYPED_STRING;
    require_storage(&probe);
  }
}

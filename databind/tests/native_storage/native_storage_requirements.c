/* #99 prerequisite: exercise the real canonical DataBind storage boundary.
 * These are migration requirements, not a direct-reader decode implementation. */
#include "data_bind_typed.h"
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
  DataBindTypedField wire;
  DataBindTypedType overlay;
  DataBindTypedDescriptor descriptor;
} ProbeDescriptor;

static ProbeStorage storage;
static const cmeta_data_buffer_ops *cleanup_buffer;

static void probe_describe(ProbeDescriptor *probe, const char *name,
                     const cmeta_data_desc *leaf, size_t size, size_t alignment,
                     size_t field_offset, size_t field_size,
                     size_t field_alignment, DataBindTypedKind wire_kind) {
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
  probe->wire = (DataBindTypedField){.name = "value", .wire_kind = wire_kind};
  probe->overlay = (DataBindTypedType){
      .name = name, .size = size, .fields = &probe->wire, .field_count = 1u};
  probe->descriptor = (DataBindTypedDescriptor)DATA_BIND_TYPED_DESCRIPTOR_INIT(
      &probe->overlay, &probe->data);
}

#define DESCRIBE(PROBE, ROW, FIELD_TYPE, LEAF, WIRE) \
  probe_describe(&(PROBE), #ROW, (LEAF), sizeof(ROW), _Alignof(ROW), \
           offsetof(ROW, value), sizeof(((ROW *)0)->value), \
           _Alignof(FIELD_TYPE), (WIRE))

static void require_storage(ProbeDescriptor *probe) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  check_true(cmeta_data_desc_valid(probe->field.value));
  check_true(cmeta_data_desc_valid(&probe->data));
  const DataBindStatus status = data_bind_typed_descriptor_validate(&probe->descriptor, &error);
  (void)printf("NATIVE_REQUIREMENT name=%s status=%d path=%s message=%s\n",
                probe->type.name, (int)status, error.path, error.message);
  check_equal(status, DATA_BIND_OK);
  check_equal(data_bind_typed_descriptor_init(&probe->descriptor, &storage, &error), DATA_BIND_OK);
  check_equal(data_bind_typed_descriptor_clear(&probe->descriptor, &storage, &error), DATA_BIND_OK);
}

static void require_owned_buffer(cmeta_data_kind kind) {
  ProbeDescriptor probe;
  cmeta_data_desc leaf = salts_tstr_cmeta_data;
  leaf.kind = kind;
  leaf.stable_id = kind == CMETA_DATA_STRING ? "test.reader.owned-text" : "test.reader.owned-bytes";
  leaf.display_name = leaf.stable_id;
  DESCRIBE(probe, ProbeBuffer, tstr, &leaf,
           kind == CMETA_DATA_STRING ? DATA_BIND_TYPED_STRING : DATA_BIND_TYPED_BYTES);
  require_storage(&probe);

  DataBindError error = DATA_BIND_ERROR_INIT;
  check_equal(data_bind_typed_descriptor_init(&probe.descriptor, &storage, &error), DATA_BIND_OK);
  cleanup_buffer = leaf.buffer_ops;
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
  static const unsigned char payload[] = {0u, 'A', 'B'};
  check_equal(cleanup_buffer->assign(&storage.buffer.value, payload, sizeof(payload), sizeof(payload)),
              CMETA_OK);
  check_false(cleanup_buffer->is_zero(&storage.buffer.value));
  check_equal(data_bind_typed_descriptor_clear(&probe.descriptor, &storage, &error), DATA_BIND_OK);
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
  /* A second clear must not free the same owning value twice. */
  check_equal(data_bind_typed_descriptor_clear(&probe.descriptor, &storage, &error), DATA_BIND_OK);
  check_true(cleanup_buffer->is_zero(&storage.buffer.value));
}

static void reject_without_touching(ProbeDescriptor *probe) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  unsigned char before[sizeof(storage)];
  memset(&storage, 0xa5, sizeof(storage));
  memcpy(before, &storage, sizeof(storage));
  check_equal(data_bind_typed_descriptor_validate(&probe->descriptor, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(data_bind_typed_descriptor_init(&probe->descriptor, &storage, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(before, &storage, sizeof(storage)), 0);
  check_equal(data_bind_typed_descriptor_clear(&probe->descriptor, &storage, &error), DATA_BIND_ERR_SCHEMA);
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
    DESCRIBE(probe, ProbeI32, int32_t, &salts_int32_cmeta_data, DATA_BIND_TYPED_I32);
    require_storage(&probe);
  }
  it("preserves canonical fixed-width unsigned storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeU64, uint64_t, &salts_uint64_cmeta_data, DATA_BIND_TYPED_U64);
    require_storage(&probe);
  }
  it("preserves the existing explicit bool8 provider") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeBool8, uint8_t, &salts_bool8_cmeta_data, DATA_BIND_TYPED_BOOL);
    require_storage(&probe);
  }
  it("preserves canonical double storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeDouble, double, &cmeta_data_double, DATA_BIND_TYPED_F64);
    require_storage(&probe);
  }
  it("accepts native C int without replacing its semantic identity") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeInt, int, &cmeta_data_int, DATA_BIND_TYPED_I32);
    require_storage(&probe);
  }
  it("accepts native C long independently of the platform width") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeLong, long, &cmeta_data_long,
             sizeof(long) == sizeof(int32_t) ? DATA_BIND_TYPED_I32 : DATA_BIND_TYPED_I64);
    require_storage(&probe);
  }
  it("accepts C bool without substituting bool8 storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeBool, bool, &cmeta_data_bool, DATA_BIND_TYPED_BOOL);
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
    DESCRIBE(probe, ProbeBuffer, tstr, &leaf, DATA_BIND_TYPED_STRING);
    reject_without_touching(&probe);
  }
  it("rejects a field offset mismatch without touching destination storage") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeI32, int32_t, &salts_int32_cmeta_data, DATA_BIND_TYPED_I32);
    ++probe.field.offset;
    reject_without_touching(&probe);
  }
  it("takes native layout from CMeta rather than obsolete overlay offsets") {
    ProbeDescriptor probe;
    DESCRIBE(probe, ProbeI32, int32_t, &salts_int32_cmeta_data, DATA_BIND_TYPED_I32);
    probe.wire.offset = SIZE_MAX;
    probe.wire.kind = DATA_BIND_TYPED_STRING;
    require_storage(&probe);
  }
}

/* A real tstr-backed provider whose empty value has a nonzero native tag.
 * The tag is part of the provider contract, not a DataBind storage convention. */
enum { PROBE_BUFFER_ZERO_TAG = 0x5a17 };

typedef struct ProbeTaggedBuffer {
  tstr text;
  unsigned tag;
} ProbeTaggedBuffer;
typedef struct ProbeTaggedRow { ProbeTaggedBuffer value; } ProbeTaggedRow;
typedef struct ProbeTaggedOuter { ProbeTaggedRow value; } ProbeTaggedOuter;

static ProbeTaggedOuter tagged_root;
static ProbeTaggedBuffer tagged_spare;
static size_t tagged_releases;

static bool tagged_is_zero(const void *object) {
  const ProbeTaggedBuffer *buffer = (const ProbeTaggedBuffer *)object;
  return buffer != NULL && buffer->tag == PROBE_BUFFER_ZERO_TAG &&
         salts_tstr_cmeta_buffer_ops.is_zero(&buffer->text);
}

static cmeta_status tagged_init_zero(void *object) {
  ProbeTaggedBuffer *buffer = (ProbeTaggedBuffer *)object;
  cmeta_status status;
  if (buffer == NULL) return CMETA_INVALID_ARGUMENT;
  status = salts_tstr_cmeta_buffer_ops.init_zero(&buffer->text);
  if (status == CMETA_OK) buffer->tag = PROBE_BUFFER_ZERO_TAG;
  return status;
}

static cmeta_status tagged_assign(void *object, const unsigned char *data,
                                   size_t size, size_t max_bytes) {
  ProbeTaggedBuffer *buffer = (ProbeTaggedBuffer *)object;
  if (!tagged_is_zero(buffer)) return CMETA_INVALID_ARGUMENT;
  return salts_tstr_cmeta_buffer_ops.assign(&buffer->text, data, size, max_bytes);
}

static void tagged_restore_zero(void *object) {
  ProbeTaggedBuffer *buffer = (ProbeTaggedBuffer *)object;
  if (buffer == NULL) return;
  if (!salts_tstr_cmeta_buffer_ops.is_zero(&buffer->text)) ++tagged_releases;
  salts_tstr_cmeta_buffer_ops.restore_zero(&buffer->text);
  buffer->tag = PROBE_BUFFER_ZERO_TAG;
}

static cmeta_status tagged_read(const void *object, const unsigned char **data,
                                 size_t *size) {
  const ProbeTaggedBuffer *buffer = (const ProbeTaggedBuffer *)object;
  if (buffer == NULL || buffer->tag != PROBE_BUFFER_ZERO_TAG)
    return CMETA_INVALID_ARGUMENT;
  return salts_tstr_cmeta_buffer_ops.read(&buffer->text, data, size);
}

static void tagged_move(void *destination, void *source) {
  ProbeTaggedBuffer *to = (ProbeTaggedBuffer *)destination;
  ProbeTaggedBuffer *from = (ProbeTaggedBuffer *)source;
  salts_tstr_cmeta_buffer_ops.move(&to->text, &from->text);
  to->tag = PROBE_BUFFER_ZERO_TAG;
  from->tag = PROBE_BUFFER_ZERO_TAG;
}

static const cmeta_type_identity tagged_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.native-storage.tagged-buffer");
static const cmeta_type_desc tagged_type = {
    .name = "ProbeTaggedBuffer", .size = sizeof(ProbeTaggedBuffer),
    .align = _Alignof(ProbeTaggedBuffer), .kind = CMETA_T_OBJECT,
    .identity = &tagged_identity};
static const cmeta_data_buffer_ops tagged_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &tagged_type, .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = tagged_is_zero, .assign = tagged_assign,
    .restore_zero = tagged_restore_zero, .read = tagged_read,
    .init_zero = tagged_init_zero, .move = tagged_move};
static const unsigned char tagged_payload[] = {'A', 0u, 'B'};

static cmeta_data_desc tagged_data(cmeta_data_kind kind) {
  cmeta_data_desc data = salts_tstr_cmeta_data;
  data.kind = kind;
  data.stable_id = kind == CMETA_DATA_STRING
      ? "test.native-storage.tagged-text" : "test.native-storage.tagged-bytes";
  data.display_name = data.stable_id;
  data.storage_type = &tagged_type;
  data.buffer_ops = &tagged_ops;
  return data;
}

static void check_tagged_payload(const ProbeTaggedBuffer *buffer) {
  const unsigned char *data = NULL;
  size_t size = 0u;
  check_equal(tagged_read(buffer, &data, &size), CMETA_OK);
  check_equal(size, sizeof(tagged_payload));
  check_not_null(data);
  check_equal(memcmp(data, tagged_payload, sizeof(tagged_payload)), 0);
}

static void require_tagged_provider(cmeta_data_kind kind) {
  const cmeta_data_desc leaf = tagged_data(kind);
  ProbeTaggedBuffer *source = &tagged_root.value.value;
  check_true(cmeta_data_desc_valid(&leaf));
  check_true(cmeta_data_buffer_ops_of(&leaf) == &tagged_ops);
  check_equal(cmeta_data_buffer_init_zero(&leaf, source), CMETA_OK);
  check_equal(cmeta_data_buffer_init_zero(&leaf, &tagged_spare), CMETA_OK);
  check_true(tagged_is_zero(source));
  check_true(tagged_is_zero(&tagged_spare));
  check_equal(cmeta_data_buffer_assign(&leaf, source, tagged_payload,
                                       sizeof(tagged_payload), sizeof(tagged_payload)), CMETA_OK);
  check_false(tagged_is_zero(source));
  check_tagged_payload(source);
  tagged_ops.move(&tagged_spare, source);
  check_true(tagged_is_zero(source));
  check_false(tagged_is_zero(&tagged_spare));
  check_tagged_payload(&tagged_spare);
  check_equal(cmeta_data_buffer_restore_zero(&leaf, &tagged_spare), CMETA_OK);
  check_equal(cmeta_data_buffer_restore_zero(&leaf, source), CMETA_OK);
  check_equal(cmeta_data_buffer_restore_zero(&leaf, &tagged_spare), CMETA_OK);
  check_true(tagged_is_zero(source));
  check_true(tagged_is_zero(&tagged_spare));
  check_equal(tagged_releases, 1u);
}

static void require_tagged_record_clear(cmeta_data_kind kind, bool nested) {
  const cmeta_data_desc leaf = tagged_data(kind);
  ProbeDescriptor row;
  ProbeDescriptor outer;
  const DataBindTypedDescriptor *descriptor;
  void *destination;
  ProbeTaggedBuffer *buffer = &tagged_root.value.value;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  DESCRIBE(row, ProbeTaggedRow, ProbeTaggedBuffer, &leaf,
           kind == CMETA_DATA_STRING ? DATA_BIND_TYPED_STRING : DATA_BIND_TYPED_BYTES);
  descriptor = &row.descriptor;
  destination = &tagged_root.value;
  if (nested) {
    DESCRIBE(outer, ProbeTaggedOuter, ProbeTaggedRow, &row.data, DATA_BIND_TYPED_OBJECT);
    outer.wire.nested_overlay = &row.overlay;
    descriptor = &outer.descriptor;
    destination = &tagged_root;
  }
  check_true(cmeta_data_desc_valid(&leaf));
  check_equal(data_bind_typed_descriptor_validate(descriptor, &error), DATA_BIND_OK);
  check_equal(data_bind_typed_descriptor_init(descriptor, destination, &error), DATA_BIND_OK);
  check_true(tagged_is_zero(buffer));
  check_equal(cmeta_data_buffer_assign(&leaf, buffer, tagged_payload,
                                       sizeof(tagged_payload), sizeof(tagged_payload)), CMETA_OK);
  check_tagged_payload(buffer);
  status = data_bind_typed_descriptor_clear(descriptor, destination, &error);
  (void)printf("SEMANTIC_ZERO kind=%s nested=%d clear_status=%d releases=%zu tag=%u zero=%d\n",
               kind == CMETA_DATA_STRING ? "text" : "bytes", (int)nested, (int)status,
               tagged_releases, buffer->tag, (int)tagged_is_zero(buffer));
  check_equal(status, DATA_BIND_OK);
  check_equal(tagged_releases, 1u);
  check_true(tagged_is_zero(buffer));
  check_equal(buffer->tag, (unsigned)PROBE_BUFFER_ZERO_TAG);
  check_equal(data_bind_typed_descriptor_clear(descriptor, destination, &error), DATA_BIND_OK);
  check_true(tagged_is_zero(buffer));
  check_equal(tagged_releases, 1u);
}

spec("DataBind preserves provider-defined nonzero semantic zero") {
  before_each() {
    tagged_root = (ProbeTaggedOuter){0};
    tagged_spare = (ProbeTaggedBuffer){0};
    tagged_releases = 0u;
  }
  after_each() {
    /* Cleanup still runs after an assertion aborts, using the real tstr owner. */
    tagged_restore_zero(&tagged_root.value.value);
    tagged_restore_zero(&tagged_spare);
  }
  it("validates the nonzero text provider lifecycle independently of DataBind") {
    require_tagged_provider(CMETA_DATA_STRING);
  }
  it("validates the nonzero bytes provider lifecycle independently of DataBind") {
    require_tagged_provider(CMETA_DATA_BYTES);
  }
  it("preserves nonzero text semantic zero after clearing a Struct") {
    require_tagged_record_clear(CMETA_DATA_STRING, false);
  }
  it("preserves nonzero bytes semantic zero after clearing a Struct") {
    require_tagged_record_clear(CMETA_DATA_BYTES, false);
  }
  it("preserves nonzero text semantic zero through nested Struct cleanup") {
    require_tagged_record_clear(CMETA_DATA_STRING, true);
  }
  it("preserves nonzero bytes semantic zero through nested Struct cleanup") {
    require_tagged_record_clear(CMETA_DATA_BYTES, true);
  }
}

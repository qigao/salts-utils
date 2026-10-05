#include "data_bind_binary_reader.h"
#include "data_bind_binary_writer.h"
#include "data_bind_message_plan.h"
#include "data_bind_native.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <salts_cmeta_data.h>
#include <tstr.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct NullableBinaryRecord {
  uint16_t required_value;
  uint16_t nullable_value;
  uint16_t tri_value;
  tstr note;
  uint8_t presence;
  uint8_t nulls;
} NullableBinaryRecord;

enum { NULLABLE_FIELD_COUNT = 4, NULLABLE_NOTE_INDEX = 3,
       NULLABLE_WORKSPACE_BYTES = 4096, NULLABLE_MAX_DEPTH = 8,
       NULLABLE_MAX_ITEMS = 16, NULLABLE_MAX_OWNED_BYTES = 64,
       BINARY_OUTPUT_BYTES = 64, BINARY_POISON = 0xa5 };

static const cmeta_type_identity NULLABLE_NATIVE_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.NullableBinary.Record");
static const cmeta_type_desc NULLABLE_NATIVE_TYPE = {
    "NullableBinaryRecord", sizeof(NullableBinaryRecord),
    _Alignof(NullableBinaryRecord), CMETA_T_OBJECT, NULL, NULL, &NULLABLE_NATIVE_ID};
static cmeta_field_desc nullable_layout_fields[] = {
    {"required_value", "uint16_t", offsetof(NullableBinaryRecord, required_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL},
    {"nullable_value", "uint16_t", offsetof(NullableBinaryRecord, nullable_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL},
    {"tri_value", "uint16_t", offsetof(NullableBinaryRecord, tri_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL},
    {"note", "tstr", offsetof(NullableBinaryRecord, note),
     sizeof(tstr), _Alignof(tstr), NULL, NULL}};
static const cmeta_struct_desc NULLABLE_NATIVE_LAYOUT = {
    "NullableBinaryRecord", sizeof(NullableBinaryRecord),
    _Alignof(NullableBinaryRecord), nullable_layout_fields, NULLABLE_FIELD_COUNT};
static cmeta_data_field_desc nullable_native_fields[] = {
    {"test.NullableBinary.required_value", "required_value",
     offsetof(NullableBinaryRecord, required_value), &cmeta_data_uint16},
    {"test.NullableBinary.nullable_value", "nullable_value",
     offsetof(NullableBinaryRecord, nullable_value), &cmeta_data_uint16},
    {"test.NullableBinary.tri_value", "tri_value",
     offsetof(NullableBinaryRecord, tri_value), &cmeta_data_uint16},
    {"test.NullableBinary.note", "note", offsetof(NullableBinaryRecord, note), NULL}};
static const cmeta_data_struct_shape NULLABLE_NATIVE_SHAPE = {
    &NULLABLE_NATIVE_LAYOUT, nullable_native_fields, NULLABLE_FIELD_COUNT};
static const cmeta_data_desc NULLABLE_NATIVE_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.NullableBinary.Record.data", .display_name = "NullableBinaryRecord",
    .kind = CMETA_DATA_STRUCT, .storage_type = &NULLABLE_NATIVE_TYPE,
    .shape = &NULLABLE_NATIVE_SHAPE};

static _Alignas(64) unsigned char nullable_workspace[NULLABLE_WORKSPACE_BYTES];
static DataBindNativeOptions nullable_options;
static DataBindNativeDiagnostic nullable_diagnostic;
static NullableBinaryRecord owned_source;
static NullableBinaryRecord owned_destination;


/* Single-threaded native lifecycle owns text and the complete host envelope.
 * Binary owns only wire state. Initialization requires unused storage, and
 * clear runs after parsing has stopped borrowing input or host pointers. */
static DataBindStatus native_init(const cmeta_data_desc *data, void *object) {
  return data_bind_native_init(&nullable_options, data, object,
                               data->storage_type->size, &nullable_diagnostic);
}

static DataBindStatus native_clear(const cmeta_data_desc *data, void *object) {
  return data_bind_native_clear(&nullable_options, data, object,
                                data->storage_type->size, &nullable_diagnostic);
}

typedef struct CanonicalBinaryRecord {
  uint16_t required_value;
  uint16_t tri_value;
  uint8_t presence;
  uint8_t nulls;
} CanonicalBinaryRecord;

static const cmeta_type_identity CANONICAL_BINARY_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.NullableBinary.Canonical");
static const cmeta_type_desc CANONICAL_BINARY_CTYPE = {
    "CanonicalBinaryRecord", sizeof(CanonicalBinaryRecord),
    _Alignof(CanonicalBinaryRecord), CMETA_T_OBJECT,
    NULL, NULL, &CANONICAL_BINARY_ID};
static const cmeta_field_desc CANONICAL_BINARY_LAYOUT_FIELDS[] = {
    {"required_value", "uint16_t", offsetof(CanonicalBinaryRecord, required_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL},
    {"tri_value", "uint16_t", offsetof(CanonicalBinaryRecord, tri_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL}};
static const cmeta_struct_desc CANONICAL_BINARY_LAYOUT = {
    "CanonicalBinaryRecord", sizeof(CanonicalBinaryRecord),
    _Alignof(CanonicalBinaryRecord), CANONICAL_BINARY_LAYOUT_FIELDS, 2u};
static const cmeta_data_field_desc CANONICAL_BINARY_DATA_FIELDS[] = {
    {"test.NullableBinary.Canonical.required_value", "required_value",
     offsetof(CanonicalBinaryRecord, required_value), &cmeta_data_uint16},
    {"test.NullableBinary.Canonical.tri_value", "tri_value",
     offsetof(CanonicalBinaryRecord, tri_value), &cmeta_data_uint16}};
static const cmeta_data_struct_shape CANONICAL_BINARY_SHAPE = {
    &CANONICAL_BINARY_LAYOUT, CANONICAL_BINARY_DATA_FIELDS, 2u};
static const cmeta_data_desc CANONICAL_BINARY_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.NullableBinary.Canonical.data",
    .display_name = "CanonicalBinaryRecord",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &CANONICAL_BINARY_CTYPE,
    .shape = &CANONICAL_BINARY_SHAPE};

static const DataBindNativeStateBinding NULLABLE_PRESENCE[] = {
    {sizeof(DataBindNativeStateBinding), "tri_value", offsetof(NullableBinaryRecord, presence), 0u},
    {sizeof(DataBindNativeStateBinding), "note", offsetof(NullableBinaryRecord, presence), 1u}};
static const DataBindNativeStateBinding NULLABLE_NULLS[] = {
    {sizeof(DataBindNativeStateBinding), "nullable_value", offsetof(NullableBinaryRecord, nulls), 0u},
    {sizeof(DataBindNativeStateBinding), "tri_value", offsetof(NullableBinaryRecord, nulls), 1u},
    {sizeof(DataBindNativeStateBinding), "note", offsetof(NullableBinaryRecord, nulls), 2u}};
static const DataBindNativeTypeBinding NULLABLE_BINDING = {
    sizeof(DataBindNativeTypeBinding), DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "NullableBinary", &NULLABLE_NATIVE_DATA, NULLABLE_PRESENCE, 2u, NULLABLE_NULLS, 3u};
static const DataBindNativeStateBinding CANONICAL_PRESENCE[] = {
    {sizeof(DataBindNativeStateBinding), "tri_value", offsetof(CanonicalBinaryRecord, presence), 0u}};
static const DataBindNativeStateBinding CANONICAL_NULLS[] = {
    {sizeof(DataBindNativeStateBinding), "tri_value", offsetof(CanonicalBinaryRecord, nulls), 0u}};
static const DataBindNativeTypeBinding CANONICAL_BINDING = {
    sizeof(DataBindNativeTypeBinding), DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "Canonical", &CANONICAL_BINARY_DATA, CANONICAL_PRESENCE, 1u, CANONICAL_NULLS, 1u};

static const DataBindBinaryFieldPlan NULLABLE_WIRE_FIELDS[] = {
    {sizeof(DataBindBinaryFieldPlan), "required_value", CSERDE_UINT, 16u,
     2u, 2u, 0u, 0u, 0u, DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "nullable_value", CSERDE_UINT, 16u,
     4u, 2u, 0u, 0u, DATA_BIND_BINARY_FIELD_NULLABLE, DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "tri_value", CSERDE_UINT, 16u,
     6u, 2u, 0u, 1u, DATA_BIND_BINARY_FIELD_OPTIONAL | DATA_BIND_BINARY_FIELD_NULLABLE,
     DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "note", CSERDE_STRING, 0u,
     0u, 0u, 1u, 2u, DATA_BIND_BINARY_FIELD_OPTIONAL | DATA_BIND_BINARY_FIELD_NULLABLE,
     DATA_BIND_BINARY_REP_VAR_DATA, 4u}};
static const DataBindBinaryFieldPlan CANONICAL_WIRE_FIELDS[] = {
    {sizeof(DataBindBinaryFieldPlan), "required_value", CSERDE_UINT, 16u,
     2u, 2u, 0u, 0u, 0u, DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "tri_value", CSERDE_UINT, 16u,
     4u, 2u, 0u, 0u, DATA_BIND_BINARY_FIELD_OPTIONAL | DATA_BIND_BINARY_FIELD_NULLABLE,
     DATA_BIND_BINARY_REP_FIXED, 0u}};

typedef struct BinaryFixture {
  const DataBindNativeTypeBinding *binding;
  DataBindBinaryLayoutPlan wire;
  DataBindMessagePlan *message;
} BinaryFixture;

typedef struct BinaryOutput {
  unsigned char bytes[BINARY_OUTPUT_BYTES];
  size_t capacity;
  size_t length;
  size_t calls;
  DataBindStatus status;
} BinaryOutput;

typedef union BinaryStaging {
  NullableBinaryRecord nullable;
  CanonicalBinaryRecord canonical;
} BinaryStaging;

static DataBind *codec;
static BinaryFixture nullable_fixture;
static BinaryFixture canonical_fixture;
static DataBindMessagePlanDiagnostic message_diagnostic;
static DataBindError error;
static cserde_reader *wire_probe_reader;
static void *wire_probe_reader_owner;
static cserde_writer *wire_probe_writer;
static void *wire_probe_writer_owner;
static const char BINARY_SCHEMA[] =
    "message NullableBinary { uint16 required_value; nullable uint16 nullable_value; "
    "optional nullable uint16 tri_value; optional nullable string note; } "
    "message Canonical { uint16 required_value; optional nullable uint16 tri_value; }";

static void fixture_init(BinaryFixture *fixture, const DataBindNativeTypeBinding *binding,
                         const DataBindBinaryFieldPlan *fields, size_t count, size_t fixed_bytes) {
  fixture->binding = binding;
  fixture->wire = (DataBindBinaryLayoutPlan)DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
  fixture->wire.type_name = binding->idl_type_name;
  fixture->wire.fixed_block_size = fixed_bytes;
  fixture->wire.presence_size = 1u;
  fixture->wire.null_offset = 1u;
  fixture->wire.null_size = 1u;
  fixture->wire.fields = fields;
  fixture->wire.field_count = count;
}

static int publish_bytes(const void *bytes, size_t size, void *context) {
  BinaryOutput *output = (BinaryOutput *)context;
  ++output->calls;
  if (size > output->capacity) {
    output->status = DATA_BIND_ERR_BUFFER_TOO_SMALL;
    return -1;
  }
  memcpy(output->bytes, bytes, size);
  output->length = size;
  return 0;
}

static DataBindStatus encode_native(const BinaryFixture *fixture, const void *object,
                                    BinaryOutput *output) {
  cserde_writer *writer = NULL;
  void *owner = NULL;
  DataBindStatus status, close_status;
  cserde_status finish_status = CSERDE_OK;
  output->length = 0u;
  output->calls = 0u;
  output->status = DATA_BIND_OK;
  status = data_bind_binary_writer_open(&fixture->wire, publish_bytes, output,
                                        NULLABLE_MAX_DEPTH, &writer, &owner, &error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_message_plan_encode_native(fixture->message, &nullable_options,
      object, fixture->binding->data->storage_type->size, writer, &message_diagnostic);
  if (status == DATA_BIND_OK) {
    finish_status = cserde_writer_finish(writer);
    if (finish_status != CSERDE_OK) status = DATA_BIND_ERR_RUNTIME;
  }
  close_status = data_bind_binary_writer_close(writer, owner, &error);
  if (status == DATA_BIND_OK || (finish_status != CSERDE_OK && close_status != DATA_BIND_OK))
    status = close_status;
  if (output->status != DATA_BIND_OK) status = output->status;
  return status;
}

static void publish_states(const DataBindNativeTypeBinding *binding, void *destination,
                            const void *source) {
  unsigned char *to = (unsigned char *)destination;
  const unsigned char *from = (const unsigned char *)source;
  size_t i;
  for (i = 0u; i < binding->presence_count; ++i) {
    size_t offset = binding->presence[i].byte_offset + binding->presence[i].bit / 8u;
    to[offset] = from[offset];
  }
  for (i = 0u; i < binding->null_count; ++i) {
    size_t offset = binding->nulls[i].byte_offset + binding->nulls[i].bit / 8u;
    to[offset] = from[offset];
  }
}

/* One thread owns fixed workspace, staging and the published message. The
 * Binary lease borrows input until close. MessagePlan copies owned payload and
 * state into staging; publication moves CMeta fields and copies DataBind state
 * only after complete decode. Every failure leaves the old owner untouched. */
static DataBindStatus replace_binary(const BinaryFixture *fixture, const void *wire,
                                     size_t size, void *destination) {
  BinaryStaging staging;
  cserde_reader *reader = NULL;
  void *owner = NULL;
  const cmeta_data_desc *data = fixture->binding->data;
  DataBindStatus status = native_init(data, &staging);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_binary_reader_open(&fixture->wire, wire, size,
      NULLABLE_MAX_DEPTH, &reader, &owner, &error);
  if (status == DATA_BIND_OK)
    status = data_bind_message_plan_decode_native(fixture->message, &nullable_options,
        reader, &staging, data->storage_type->size, &message_diagnostic);
  data_bind_binary_reader_close(reader, owner);
  if (status == DATA_BIND_OK) {
    cmeta_data_value_destroy(data, destination);
    cmeta_data_trait_move_construct(data, destination, &staging);
    publish_states(fixture->binding, destination, &staging);
  }
  cmeta_data_value_destroy(data, &staging);
  return status;
}

spec("DataBind canonical Binary native ownership and state") {
  before_all() {
    nullable_layout_fields[NULLABLE_NOTE_INDEX].type = salts_tstr_cmeta_data.storage_type;
    nullable_native_fields[NULLABLE_NOTE_INDEX].value = &salts_tstr_cmeta_data;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    message_diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    fixture_init(&nullable_fixture, &NULLABLE_BINDING, NULLABLE_WIRE_FIELDS, NULLABLE_FIELD_COUNT, 8u);
    fixture_init(&canonical_fixture, &CANONICAL_BINDING, CANONICAL_WIRE_FIELDS, 2u, 6u);
    check_equal(data_bind_binary_layout_plan_validate(&nullable_fixture.wire, &error), DATA_BIND_OK);
    check_equal(data_bind_binary_layout_plan_validate(&canonical_fixture.wire, &error), DATA_BIND_OK);
    check_equal(data_bind_create_from_text(BINARY_SCHEMA, sizeof(BINARY_SCHEMA) - 1u,
                                          &codec, &error), DATA_BIND_OK);
    check_equal(data_bind_message_plan_compile(codec, NULLABLE_BINDING.idl_type_name,
        &NULLABLE_BINDING, &nullable_fixture.message, &message_diagnostic), DATA_BIND_OK);
    check_equal(data_bind_message_plan_compile(codec, CANONICAL_BINDING.idl_type_name,
        &CANONICAL_BINDING, &canonical_fixture.message, &message_diagnostic), DATA_BIND_OK);
  }
  before_each() {
    nullable_options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    nullable_diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    message_diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    nullable_options.workspace = nullable_workspace;
    nullable_options.workspace_bytes = sizeof(nullable_workspace);
    nullable_options.max_depth = NULLABLE_MAX_DEPTH;
    nullable_options.max_items = NULLABLE_MAX_ITEMS;
    nullable_options.max_owned_bytes = NULLABLE_MAX_OWNED_BYTES;
    check_equal(native_init(&NULLABLE_NATIVE_DATA, &owned_source), DATA_BIND_OK);
    check_equal(native_init(&NULLABLE_NATIVE_DATA, &owned_destination), DATA_BIND_OK);
  }
  after_each() {
    (void)cmeta_data_value_restore_zero(&NULLABLE_NATIVE_DATA, &owned_source);
    (void)cmeta_data_value_restore_zero(&NULLABLE_NATIVE_DATA, &owned_destination);
    data_bind_binary_reader_close(wire_probe_reader, wire_probe_reader_owner);
    if (wire_probe_writer != NULL)
      (void)data_bind_binary_writer_close(wire_probe_writer, wire_probe_writer_owner, NULL);
    wire_probe_reader = NULL;
    wire_probe_reader_owner = NULL;
    wire_probe_writer = NULL;
    wire_probe_writer_owner = NULL;
  }
  after_all() {
    data_bind_message_plan_free(nullable_fixture.message);
    data_bind_message_plan_free(canonical_fixture.message);
    data_bind_free(codec);
  }

  it("preserves old owners on failure and publishes independent native text") {
    static const unsigned char payload[] = "owned";
    static const unsigned char previous[] = "keep";
    BinaryOutput output = {.capacity = BINARY_OUTPUT_BYTES};
    unsigned char before[sizeof(owned_destination)];
    tstr previous_owner;
    check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &owned_source.note,
                payload, sizeof(payload) - 1u, NULLABLE_MAX_OWNED_BYTES), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &owned_destination.note,
                previous, sizeof(previous) - 1u, NULLABLE_MAX_OWNED_BYTES), CMETA_OK);
    owned_source.required_value = 7u;
    owned_source.tri_value = 9u;
    owned_source.presence = 3u;
    previous_owner = owned_destination.note;
    memcpy(before, &owned_destination, sizeof(before));
    check_equal(encode_native(&nullable_fixture, &owned_source, &output), DATA_BIND_OK);
    check_equal(replace_binary(&nullable_fixture, output.bytes, output.length - 1u,
                               &owned_destination), DATA_BIND_ERR_PARSE);
    check_equal(&owned_destination, before, sizeof(before));
    check_true(owned_destination.note == previous_owner);
    check_equal(memcmp(owned_destination.note, previous, sizeof(previous) - 1u), 0);
    check_equal(replace_binary(&nullable_fixture, output.bytes, output.length,
                               &owned_destination), DATA_BIND_OK);
    check_true(owned_destination.note != owned_source.note);
    check_equal(native_clear(&NULLABLE_NATIVE_DATA, &owned_source), DATA_BIND_OK);
    memset(output.bytes, BINARY_POISON, sizeof(output.bytes));
    check_equal(owned_destination.required_value, 7u);
    check_equal(owned_destination.tri_value, 9u);
    check_equal(tstr_len(owned_destination.note), sizeof(payload) - 1u);
    check_equal(memcmp(owned_destination.note, payload, sizeof(payload) - 1u), 0);
    check_equal(native_clear(&NULLABLE_NATIVE_DATA, &owned_destination), DATA_BIND_OK);
    check_equal(native_clear(&NULLABLE_NATIVE_DATA, &owned_destination), DATA_BIND_OK);
    check_null(owned_destination.note);
    check_equal(owned_destination.presence, 0u);
    check_equal(owned_destination.nulls, 0u);
  }

  it("preserves the original dual state and NULL tail bytes in both wire orders") {
    static const unsigned char expected[][12] = {
        {3u, 5u, 7u, 0u, 0u, 0u, 9u, 0u, 0u, 0u, 0u, 0u},
        {3u, 5u, 0u, 7u, 0u, 0u, 0u, 9u, 0u, 0u, 0u, 0u}};
    BinaryFixture fixture = nullable_fixture;
    BinaryOutput output = {.capacity = BINARY_OUTPUT_BYTES};
    owned_source.required_value = 7u;
    owned_source.nullable_value = 123u;
    owned_source.tri_value = 9u;
    owned_source.presence = 3u;
    owned_source.nulls = 5u;
    for (int order = 0; order <= 1; ++order) {
      fixture.wire.wire_big_endian = order;
      check_equal(encode_native(&fixture, &owned_source, &output), DATA_BIND_OK);
      check_equal(output.length, sizeof(expected[order]));
      check_equal(output.bytes, expected[order], sizeof(expected[order]));
      check_equal(replace_binary(&fixture, output.bytes, output.length,
                                 &owned_destination), DATA_BIND_OK);
      check_equal(owned_destination.required_value, 7u);
      check_equal(owned_destination.nullable_value, 0u);
      check_equal(owned_destination.tri_value, 9u);
      check_null(owned_destination.note);
      check_equal(owned_destination.presence, owned_source.presence);
      check_equal(owned_destination.nulls, owned_source.nulls);
    }
  }

  it("rejects impossible host state before any wire publication") {
    BinaryOutput output = {.capacity = BINARY_OUTPUT_BYTES};
    owned_source.required_value = 1u;
    owned_source.nulls = 2u;
    memset(output.bytes, BINARY_POISON, sizeof(output.bytes));
    check_equal(encode_native(&nullable_fixture, &owned_source, &output), DATA_BIND_ERR_SCHEMA);
    check_equal(output.length, (size_t)0u);
    check_equal(output.calls, (size_t)0u);
    for (size_t i = 0u; i < sizeof(output.bytes); ++i)
      check_equal(output.bytes[i], BINARY_POISON);
  }

  it("rejects nonempty NULL tail payload before touching published storage") {
    static const unsigned char malformed[] = {2u, 4u, 1u, 0u, 0u, 0u, 0u, 0u,
                                               1u, 0u, 0u, 0u, 'x'};
    unsigned char before[sizeof(owned_destination)];
    owned_destination.required_value = 99u;
    memcpy(before, &owned_destination, sizeof(before));
    check_equal(replace_binary(&nullable_fixture, malformed, sizeof(malformed),
                               &owned_destination), DATA_BIND_ERR_PARSE);
    check_contains(error.message, "NULL");
    check_equal(&owned_destination, before, sizeof(before));
  }

  it("preserves the fixed canonical NULL state and original six wire bytes") {
    static const unsigned char expected[][6] = {
        {1u, 1u, 7u, 0u, 0u, 0u}, {1u, 1u, 0u, 7u, 0u, 0u}};
    CanonicalBinaryRecord source = {7u, 99u, 1u, 1u};
    CanonicalBinaryRecord destination = {0};
    BinaryFixture fixture = canonical_fixture;
    BinaryOutput output = {.capacity = BINARY_OUTPUT_BYTES};
    for (int order = 0; order <= 1; ++order) {
      fixture.wire.wire_big_endian = order;
      check_equal(encode_native(&fixture, &source, &output), DATA_BIND_OK);
      check_equal(output.length, sizeof(expected[order]));
      check_equal(output.bytes, expected[order], sizeof(expected[order]));
      check_equal(replace_binary(&fixture, output.bytes, output.length, &destination), DATA_BIND_OK);
      check_equal(destination.required_value, 7u);
      check_equal(destination.tri_value, 0u);
      check_equal(destination.presence, 1u);
      check_equal(destination.nulls, 1u);
    }
  }

  it("keeps fixed output untouched when canonical native state is contradictory") {
    CanonicalBinaryRecord source = {1u, 0u, 0u, 1u};
    BinaryOutput output = {.capacity = BINARY_OUTPUT_BYTES};
    unsigned char before[sizeof(output.bytes)];
    memset(output.bytes, BINARY_POISON, sizeof(output.bytes));
    memcpy(before, output.bytes, sizeof(before));
    check_equal(encode_native(&canonical_fixture, &source, &output), DATA_BIND_ERR_SCHEMA);
    check_equal(output.length, (size_t)0u);
    check_equal(output.calls, (size_t)0u);
    check_equal(output.bytes, before, sizeof(before));
  }

  it("preserves fixed native storage on wire size and state rejection") {
    static const unsigned char invalid_state[] = {0u, 1u, 7u, 0u, 0u, 0u};
    static const unsigned char trailing[] = {1u, 0u, 7u, 0u, 9u, 0u, 0u};
    CanonicalBinaryRecord destination = {17u, 19u, 1u, 0u};
    unsigned char before[sizeof(destination)];
    memcpy(before, &destination, sizeof(before));
    check_equal(replace_binary(&canonical_fixture, trailing,
        canonical_fixture.wire.fixed_block_size - 1u, &destination), DATA_BIND_ERR_PARSE);
    check_equal(&destination, before, sizeof(before));
    check_equal(replace_binary(&canonical_fixture, trailing, sizeof(trailing),
                               &destination), DATA_BIND_ERR_PARSE);
    check_equal(&destination, before, sizeof(before));
    check_equal(replace_binary(&canonical_fixture, invalid_state, sizeof(invalid_state),
                               &destination), DATA_BIND_ERR_PARSE);
    check_equal(&destination, before, sizeof(before));
  }

  it("retains the old owner on decode budget failure and permits retry") {
    static const unsigned char payload[] = "owned";
    static const unsigned char previous[] = "keep";
    BinaryOutput output = {.capacity = BINARY_OUTPUT_BYTES};
    unsigned char before[sizeof(owned_destination)];
    check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &owned_source.note,
                payload, sizeof(payload) - 1u, NULLABLE_MAX_OWNED_BYTES), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &owned_destination.note,
                previous, sizeof(previous) - 1u, NULLABLE_MAX_OWNED_BYTES), CMETA_OK);
    owned_source.presence = 2u;
    check_equal(encode_native(&nullable_fixture, &owned_source, &output), DATA_BIND_OK);
    memcpy(before, &owned_destination, sizeof(before));
    nullable_options.max_owned_bytes = sizeof(payload) - 2u;
    check_equal(replace_binary(&nullable_fixture, output.bytes, output.length,
                               &owned_destination), DATA_BIND_ERR_LIMIT);
    check_equal(&owned_destination, before, sizeof(before));
    check_equal(memcmp(owned_destination.note, previous, sizeof(previous) - 1u), 0);
    nullable_options.max_owned_bytes = NULLABLE_MAX_OWNED_BYTES;
    check_equal(replace_binary(&nullable_fixture, output.bytes, output.length,
                               &owned_destination), DATA_BIND_OK);
    check_equal(memcmp(owned_destination.note, payload, sizeof(payload) - 1u), 0);
  }

  it("publishes once and preserves bounded output on short buffer failure") {
    CanonicalBinaryRecord source = {7u, 9u, 1u, 0u};
    BinaryOutput output = {.capacity = 1u};
    unsigned char before[sizeof(output.bytes)];
    memset(output.bytes, BINARY_POISON, sizeof(output.bytes));
    memcpy(before, output.bytes, sizeof(before));
    check_equal(encode_native(&canonical_fixture, &source, &output), DATA_BIND_ERR_BUFFER_TOO_SMALL);
    check_equal(output.calls, (size_t)1u);
    check_equal(output.length, (size_t)0u);
    check_equal(output.bytes, before, sizeof(before));
    output.capacity = sizeof(output.bytes);
    check_equal(encode_native(&canonical_fixture, &source, &output), DATA_BIND_OK);
    check_equal(output.calls, (size_t)1u);
    check_equal(output.length, canonical_fixture.wire.fixed_block_size);
  }

  it("preserves the original big endian Packet wire through canonical reader and writer") {
    static const unsigned char golden[] = {1u, 0x12u, 0x34u, 1u, 2u, 3u, 4u,
                                            0u, 0u, 0u, 3u, 'A', 'B', 'C'};
    static const DataBindBinaryFieldPlan fields[] = {
        {sizeof(DataBindBinaryFieldPlan), "code", CSERDE_UINT, 16u,
         1u, 2u, 0u, 0u, DATA_BIND_BINARY_FIELD_OPTIONAL, DATA_BIND_BINARY_REP_FIXED, 0u},
        {sizeof(DataBindBinaryFieldPlan), "count", CSERDE_UINT, 32u,
         3u, 4u, 0u, 0u, 0u, DATA_BIND_BINARY_REP_FIXED, 0u},
        {sizeof(DataBindBinaryFieldPlan), "name", CSERDE_STRING, 0u,
         0u, 0u, 0u, 0u, 0u, DATA_BIND_BINARY_REP_VAR_DATA, 4u}};
    DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
    BinaryOutput output = {.capacity = BINARY_OUTPUT_BYTES};
    cserde_token token;
    cserde_status token_status;
    plan.type_name = "Packet";
    plan.wire_big_endian = 1;
    plan.fixed_block_size = 7u;
    plan.presence_size = 1u;
    plan.null_offset = plan.presence_size;
    plan.fields = fields;
    plan.field_count = sizeof(fields) / sizeof(fields[0]);
    check_equal(data_bind_binary_reader_open(&plan, golden, sizeof(golden),
        NULLABLE_MAX_DEPTH, &wire_probe_reader, &wire_probe_reader_owner, &error), DATA_BIND_OK);
    check_equal(data_bind_binary_writer_open(&plan, publish_bytes, &output,
        NULLABLE_MAX_DEPTH, &wire_probe_writer, &wire_probe_writer_owner, &error), DATA_BIND_OK);
    while ((token_status = cserde_reader_next(wire_probe_reader, &token)) == CSERDE_OK)
      check_equal(cserde_writer_write(wire_probe_writer, &token), CSERDE_OK);
    check_equal(token_status, CSERDE_DONE);
    check_equal(cserde_writer_finish(wire_probe_writer), CSERDE_OK);
    check_equal(output.calls, (size_t)1u);
    check_equal(output.length, sizeof(golden));
    check_equal(output.bytes, golden, sizeof(golden));
  }

  it("preserves unsigned 64 bit wire values without signed narrowing") {
    static const unsigned char golden[] = {0xffu, 0xffu, 0xffu, 0xffu,
                                            0xffu, 0xffu, 0xffu, 0xffu};
    static const DataBindBinaryFieldPlan fields[] = {
        {sizeof(DataBindBinaryFieldPlan), "value", CSERDE_UINT, 64u,
         0u, 8u, 0u, 0u, 0u, DATA_BIND_BINARY_REP_FIXED, 0u}};
    DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
    BinaryOutput output = {.capacity = BINARY_OUTPUT_BYTES};
    cserde_token token;
    enum { SCALAR_TOKEN_COUNT = 4, SCALAR_VALUE_TOKEN = 2 };
    plan.type_name = "UnsignedBits";
    plan.fixed_block_size = sizeof(golden);
    plan.fields = fields;
    plan.field_count = sizeof(fields) / sizeof(fields[0]);
    for (int order = 0; order <= 1; ++order) {
      plan.wire_big_endian = order;
      check_equal(data_bind_binary_reader_open(&plan, golden, sizeof(golden),
          NULLABLE_MAX_DEPTH, &wire_probe_reader, &wire_probe_reader_owner, &error), DATA_BIND_OK);
      check_equal(data_bind_binary_writer_open(&plan, publish_bytes, &output,
          NULLABLE_MAX_DEPTH, &wire_probe_writer, &wire_probe_writer_owner, &error), DATA_BIND_OK);
      for (size_t i = 0u; i < SCALAR_TOKEN_COUNT; ++i) {
        check_equal(cserde_reader_next(wire_probe_reader, &token), CSERDE_OK);
        if (i == SCALAR_VALUE_TOKEN) {
          check_equal(token.kind, CSERDE_UINT);
          check_equal(token.value.uint, UINT64_MAX);
        }
        check_equal(cserde_writer_write(wire_probe_writer, &token), CSERDE_OK);
      }
      check_equal(cserde_reader_next(wire_probe_reader, &token), CSERDE_DONE);
      check_equal(cserde_writer_finish(wire_probe_writer), CSERDE_OK);
      check_equal(output.length, sizeof(golden));
      check_equal(output.bytes, golden, sizeof(golden));
      data_bind_binary_reader_close(wire_probe_reader, wire_probe_reader_owner);
      wire_probe_reader = NULL;
      wire_probe_reader_owner = NULL;
      check_equal(data_bind_binary_writer_close(wire_probe_writer, wire_probe_writer_owner, &error),
                  DATA_BIND_OK);
      wire_probe_writer = NULL;
      wire_probe_writer_owner = NULL;
      output.calls = 0u;
      output.length = 0u;
    }
  }
}

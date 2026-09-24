#include "tbe_typed.h"
#include "tinytest.h"

#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { BOUNDARY_STORAGE_SIZE = 128, BOUNDARY_SENTINEL = 0xa5 };

#ifdef _MSC_VER
typedef union BoundaryMaxAlign {
  long double long_double;
  void *pointer;
  long long integer;
} BoundaryMaxAlign;
#define BOUNDARY_ALIGNMENT_TYPE BoundaryMaxAlign
#else
#define BOUNDARY_ALIGNMENT_TYPE max_align_t
#endif

typedef union BoundaryStorage {
  BOUNDARY_ALIGNMENT_TYPE alignment;
  uint8_t bytes[BOUNDARY_STORAGE_SIZE];
} BoundaryStorage;

#undef BOUNDARY_ALIGNMENT_TYPE
_Static_assert(sizeof(vec_t) <= sizeof(BoundaryStorage), "Boundary fixture must hold a vector");

static const cmeta_type_identity BOUNDARY_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.BoundaryStorage");
static const cmeta_type_desc BOUNDARY_CMETA_TYPE = {
    "BoundaryStorage", sizeof(BoundaryStorage), _Alignof(BoundaryStorage),
    CMETA_T_OBJECT, NULL, NULL, &BOUNDARY_ID};
static const cmeta_struct_desc BOUNDARY_LAYOUT = {
    "BoundaryStorage", sizeof(BoundaryStorage), _Alignof(BoundaryStorage),
    NULL, 0u};
static const cmeta_data_struct_shape BOUNDARY_SHAPE = {
    &BOUNDARY_LAYOUT, NULL, 0u};
static const cmeta_data_desc BOUNDARY_DATA = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.BoundaryStorage.data", "BoundaryStorage", CMETA_DATA_STRUCT,
    &BOUNDARY_CMETA_TYPE, &BOUNDARY_SHAPE, NULL, NULL, NULL};

typedef struct StateBoundary {
  uint32_t value;
  uint8_t presence;
  uint8_t nulls;
} StateBoundary;

static const cmeta_type_identity STATE_BOUNDARY_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.StateBoundary");
static const cmeta_type_desc STATE_BOUNDARY_TYPE = {
    "StateBoundary", sizeof(StateBoundary), _Alignof(StateBoundary),
    CMETA_T_OBJECT, NULL, NULL, &STATE_BOUNDARY_ID};
static const cmeta_field_desc STATE_BOUNDARY_LAYOUT_FIELDS[] = {
    {"value", "uint32_t", offsetof(StateBoundary, value), sizeof(uint32_t),
     _Alignof(uint32_t), &salts_uint32_cmeta_type, NULL}};
static const cmeta_struct_desc STATE_BOUNDARY_LAYOUT = {
    "StateBoundary", sizeof(StateBoundary), _Alignof(StateBoundary),
    STATE_BOUNDARY_LAYOUT_FIELDS, 1u};
static const cmeta_data_field_desc STATE_BOUNDARY_FIELDS[] = {
    {"test.StateBoundary.value", "value", offsetof(StateBoundary, value),
     &salts_uint32_cmeta_data}};
static const cmeta_data_struct_shape STATE_BOUNDARY_SHAPE = {
    &STATE_BOUNDARY_LAYOUT, STATE_BOUNDARY_FIELDS, 1u};
static const cmeta_data_desc STATE_BOUNDARY_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.StateBoundary.data",
    .display_name = "StateBoundary",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &STATE_BOUNDARY_TYPE,
    .shape = &STATE_BOUNDARY_SHAPE};

static const TbeTypedField STATE_BOUNDARY_TYPED_FIELDS[] = {
    {.name = "value",
     .kind = TBE_TYPED_U32,
     .wire_kind = TBE_TYPED_U32,
     .offset = offsetof(StateBoundary, value),
     .optional_bit = 0u,
     .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_NULLABLE,
     .nullable_bit = 0u}};
static const TbeTypedType STATE_BOUNDARY_OVERLAY = {
    .name = "StateBoundary",
    .size = sizeof(StateBoundary),
    .fields = STATE_BOUNDARY_TYPED_FIELDS,
    .field_count = 1u,
    .fixed_block_size = 0u,
    .presence_offset = offsetof(StateBoundary, presence),
    .presence_size = 1u,
    .wire_big_endian = 0,
    .null_offset = offsetof(StateBoundary, nulls),
    .null_size = 1u};
static const TbeTypedDescriptor STATE_BOUNDARY_DESCRIPTOR =
    TBE_TYPED_DESCRIPTOR_INIT(&STATE_BOUNDARY_OVERLAY, &STATE_BOUNDARY_DATA);

static TbeTypedType boundary_type(const TbeTypedField *fields, size_t count) {
  TbeTypedType type = {
      .name = "Boundary", .size = sizeof(BoundaryStorage),
      .fields = fields, .field_count = count, .fixed_block_size = 8u};
  return type;
}

/* A one-byte input/output also proves schema errors take precedence over
 * truncation/capacity errors. Poisoned owning storage must never be inspected. */
static void expect_binary_rejection(const TbeTypedType *type) {
  BoundaryStorage object;
  uint8_t before[sizeof(object)];
  const uint8_t input = BOUNDARY_SENTINEL;
  uint8_t output = BOUNDARY_SENTINEL;
  size_t out_len = sizeof(object);
  DataBindError error = DATA_BIND_ERROR_INIT;
  memset(&object, BOUNDARY_SENTINEL, sizeof(object));
  memcpy(before, &object, sizeof(object));

  check_equal(tbe_typed_parse_binary(type, &input, sizeof(input), &object, &error),
              DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  check_equal(tbe_typed_serialize_binary_into(type, &object, &output, sizeof(output),
                                             &out_len, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(out_len, 0u);
  check_equal(output, BOUNDARY_SENTINEL);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
}

/* Host-only APIs deliberately do not require a binary layout: text-only
 * descriptors remain valid. Only malformed host layouts reach this helper. */
static void expect_host_rejection(const TbeTypedType *type) {
  BoundaryStorage object;
  uint8_t before[sizeof(object)];
  DataBindError error = DATA_BIND_ERROR_INIT;
  memset(&object, BOUNDARY_SENTINEL, sizeof(object));
  memcpy(before, &object, sizeof(object));

  check_equal(tbe_typed_validate_descriptor(type, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(tbe_typed_init(type, &object, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  tbe_typed_clear(type, &object);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  expect_binary_rejection(type);
}

static void expect_descriptor_rejection(const TbeTypedDescriptor *descriptor) {
  BoundaryStorage object;
  uint8_t before[sizeof(object)];
  const uint8_t input[] = {'{', '}'};
  DataBindError error = DATA_BIND_ERROR_INIT;
  memset(&object, BOUNDARY_SENTINEL, sizeof(object));
  memcpy(before, &object, sizeof(object));

  check_equal(tbe_typed_descriptor_validate(descriptor, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(tbe_typed_descriptor_init(descriptor, &object, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  check_equal(tbe_typed_descriptor_parse(NULL, "Boundary", descriptor,
                                         DATA_BIND_FORMAT_JSON, input,
                                         sizeof(input), 0u, &object, &error),
              DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  check_equal(tbe_typed_descriptor_clear(descriptor, &object, &error),
              DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
}

spec("typed descriptor boundary") {
  it("rejects owning overlap before initialization or cleanup touches storage") {
    const TbeTypedField fields[] = {
        {.name = "a", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING},
        {.name = "b", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING}};
    TbeTypedType type = boundary_type(fields, 2u);
    expect_host_rejection(&type);
  }

  it("rejects scalar then owner overlap independently of declaration order") {
    const TbeTypedField fields[] = {
        {.name = "bits", .kind = TBE_TYPED_U32, .wire_kind = TBE_TYPED_U32},
        {.name = "owner", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING}};
    TbeTypedType type = boundary_type(fields, 2u);
    expect_host_rejection(&type);
  }

  it("rejects presence overlapping owning storage before lifecycle operations") {
    const TbeTypedField field = {
        .name = "owner", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING};
    TbeTypedType type = boundary_type(&field, 1u);
    type.presence_size = 1u;
    expect_host_rejection(&type);
  }

  it("rejects owning fixed arrays overlapping scalar storage") {
    const TbeTypedField fields[] = {
        {.name = "owners", .kind = TBE_TYPED_FIXED_ARRAY,
         .element_kind = TBE_TYPED_STRING, .element_wire_kind = TBE_TYPED_STRING,
         .element_size = sizeof(tstr), .fixed_count = 2u},
        {.name = "bits", .kind = TBE_TYPED_U32, .wire_kind = TBE_TYPED_U32,
         .offset = sizeof(tstr)}};
    TbeTypedType type = boundary_type(fields, 2u);
    expect_host_rejection(&type);
  }

  it("rejects invalid nested ownership before touching the parent object") {
    const TbeTypedField children[] = {
        {.name = "a", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING},
        {.name = "b", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING}};
    TbeTypedType child = boundary_type(children, 2u);
    const TbeTypedField field = {
        .name = "child", .kind = TBE_TYPED_OBJECT, .object_type = &child};
    TbeTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects host offset addition overflow") {
    const TbeTypedField field = {
        .name = "value", .kind = TBE_TYPED_U32, .wire_kind = TBE_TYPED_U32,
        .offset = SIZE_MAX - 1u};
    TbeTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects host presence addition overflow") {
    TbeTypedType type = boundary_type(NULL, 0u);
    type.presence_offset = SIZE_MAX - 1u;
    type.presence_size = sizeof(uint32_t);
    expect_host_rejection(&type);
  }

  it("rejects fixed array host extent multiplication overflow") {
    const TbeTypedField field = {
        .name = "values", .kind = TBE_TYPED_FIXED_ARRAY,
        .element_kind = TBE_TYPED_U32, .element_wire_kind = TBE_TYPED_U32,
        .element_size = sizeof(uint32_t),
        .fixed_count = SIZE_MAX / sizeof(uint32_t) + 1u};
    TbeTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects map key and scalar value overlap before lifecycle operations") {
    const TbeTypedField field = {
        .name = "entries", .kind = TBE_TYPED_MAP,
        .element_size = sizeof(tstr), .map_entry_size = sizeof(tstr),
        .map_value_kind = TBE_TYPED_U32, .map_value_wire_kind = TBE_TYPED_U32};
    TbeTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects map key interval addition overflow") {
    const TbeTypedField field = {
        .name = "entries", .kind = TBE_TYPED_MAP,
        .element_size = SIZE_MAX, .map_entry_size = SIZE_MAX,
        .map_key_offset = SIZE_MAX - sizeof(tstr) + 1u,
        .map_value_kind = TBE_TYPED_U32, .map_value_wire_kind = TBE_TYPED_U32};
    TbeTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects map value interval addition overflow") {
    const TbeTypedField field = {
        .name = "entries", .kind = TBE_TYPED_MAP,
        .element_size = SIZE_MAX, .map_entry_size = SIZE_MAX,
        .map_value_offset = SIZE_MAX - 1u,
        .map_value_kind = TBE_TYPED_U32, .map_value_wire_kind = TBE_TYPED_U32};
    TbeTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects oversized wire presence before decoding or writing") {
    TbeTypedType type = boundary_type(NULL, 0u);
    type.fixed_block_size = 1u;
    type.presence_size = 8u;
    expect_binary_rejection(&type);
  }

  it("rejects overlapping wire fields before decoding or writing") {
    const TbeTypedField fields[] = {
        {.name = "a", .kind = TBE_TYPED_U32, .wire_kind = TBE_TYPED_U32,
         .wire_offset = 0u, .wire_size = 4u, .flags = TBE_TYPED_FIELD_WIRE_OFFSET},
        {.name = "b", .kind = TBE_TYPED_U32, .wire_kind = TBE_TYPED_U32,
         .offset = sizeof(uint32_t), .wire_offset = 2u, .wire_size = 4u,
         .flags = TBE_TYPED_FIELD_WIRE_OFFSET}};
    TbeTypedType type = boundary_type(fields, 2u);
    expect_binary_rejection(&type);
  }

  it("rejects wire fields overlapping presence before decoding or writing") {
    const TbeTypedField field = {
        .name = "value", .kind = TBE_TYPED_U32, .wire_kind = TBE_TYPED_U32,
        .wire_size = 4u, .flags = TBE_TYPED_FIELD_WIRE_OFFSET};
    TbeTypedType type = boundary_type(&field, 1u);
    type.presence_offset = sizeof(uint32_t);
    type.presence_size = 1u;
    expect_binary_rejection(&type);
  }

  it("rejects wire size disagreement before decoding or writing") {
    const TbeTypedField field = {
        .name = "value", .kind = TBE_TYPED_U32, .wire_kind = TBE_TYPED_U32,
        .wire_size = 1u, .flags = TBE_TYPED_FIELD_WIRE_OFFSET};
    TbeTypedType type = boundary_type(&field, 1u);
    expect_binary_rejection(&type);
  }

  it("rejects fixed wire interval addition overflow") {
    const TbeTypedField field = {
        .name = "value", .kind = TBE_TYPED_U32, .wire_kind = TBE_TYPED_U32,
        .wire_offset = SIZE_MAX - 1u, .wire_size = 4u,
        .flags = TBE_TYPED_FIELD_WIRE_OFFSET};
    TbeTypedType type = boundary_type(&field, 1u);
    type.fixed_block_size = SIZE_MAX;
    expect_binary_rejection(&type);
  }

  it("rejects array wire extent overflow even when its host extent fits") {
    const TbeTypedType child = {
        .name = "WideWireChild", .size = 1u,
        .fixed_block_size = SIZE_MAX / 2u + 1u};
    const TbeTypedField field = {
        .name = "children", .kind = TBE_TYPED_FIXED_ARRAY,
        .element_kind = TBE_TYPED_OBJECT, .element_size = 1u,
        .fixed_count = 2u, .object_type = &child,
        .flags = TBE_TYPED_FIELD_WIRE_OFFSET};
    TbeTypedType type = boundary_type(&field, 1u);
    DataBindError error = DATA_BIND_ERROR_INIT;
    type.fixed_block_size = SIZE_MAX;
    check_equal(tbe_typed_validate_descriptor(&type, &error), DATA_BIND_OK);
    expect_binary_rejection(&type);
  }

  it("keeps adjacent owning fields and host presence valid for text bindings") {
    typedef struct AdjacentOwners { tstr owners[2]; uint8_t presence; } AdjacentOwners;
    const TbeTypedField fields[] = {
        {.name = "a", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING},
        {.name = "b", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING,
         .offset = sizeof(tstr)}};
    TbeTypedType type = boundary_type(fields, 2u);
    AdjacentOwners object;
    DataBindError error = DATA_BIND_ERROR_INIT;
    type.size = sizeof(object);
    type.fixed_block_size = 0u;
    type.presence_offset = offsetof(AdjacentOwners, presence);
    type.presence_size = 1u;
    check_equal(tbe_typed_validate_descriptor(&type, &error), DATA_BIND_OK);
    check_equal(tbe_typed_init(&type, &object, &error), DATA_BIND_OK);
    tbe_typed_clear(&type, &object);
  }

  it("keeps empty host intervals disjoint from owning storage") {
    const TbeTypedField fields[] = {
        {.name = "empty", .kind = TBE_TYPED_FIXED_BYTES, .fixed_count = 0u},
        {.name = "owner", .kind = TBE_TYPED_STRING, .wire_kind = TBE_TYPED_STRING}};
    TbeTypedType type = boundary_type(fields, 2u);
    BoundaryStorage object;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(tbe_typed_validate_descriptor(&type, &error), DATA_BIND_OK);
    check_equal(tbe_typed_init(&type, &object, &error), DATA_BIND_OK);
    tbe_typed_clear(&type, &object);
  }

  it("requires an ABI-v3 descriptor with one canonical Struct graph") {
    const TbeTypedField overlay_field = {
        .name = "value", .kind = TBE_TYPED_U8, .wire_kind = TBE_TYPED_U8};
    TbeTypedType overlay = boundary_type(NULL, 0u);
    TbeTypedType mismatched_overlay = boundary_type(&overlay_field, 1u);
    TbeTypedDescriptor descriptor =
        TBE_TYPED_DESCRIPTOR_INIT(&overlay, &BOUNDARY_DATA);
    TbeTypedDescriptor rejected;
    cmeta_data_desc bad_data;
    DataBindError error = DATA_BIND_ERROR_INIT;

    overlay.fixed_block_size = 0u;
    mismatched_overlay.fixed_block_size = 0u;
    check_equal(tbe_typed_descriptor_validate(&descriptor, &error), DATA_BIND_OK);

    rejected = descriptor;
    rejected.struct_size = offsetof(TbeTypedDescriptor, native_data);
    expect_descriptor_rejection(&rejected);

    rejected = descriptor;
    rejected.abi_version = 1u;
    expect_descriptor_rejection(&rejected);

    rejected = descriptor;
    rejected.abi_version = 2u;
    expect_descriptor_rejection(&rejected);

    rejected = descriptor;
    rejected.native_data = NULL;
    expect_descriptor_rejection(&rejected);

    bad_data = BOUNDARY_DATA;
    bad_data.kind = CMETA_DATA_BOOL;
    rejected = descriptor;
    rejected.native_data = &bad_data;
    expect_descriptor_rejection(&rejected);

    bad_data = BOUNDARY_DATA;
    bad_data.shape = NULL;
    rejected = descriptor;
    rejected.native_data = &bad_data;
    expect_descriptor_rejection(&rejected);

    rejected = descriptor;
    rejected.overlay = &mismatched_overlay;
    expect_descriptor_rejection(&rejected);
  }

  it("validates and clears independent presence and null overlays") {
    StateBoundary object = {
        .value = 99u,
        .presence = 0xffu,
        .nulls = 0xffu};
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        tbe_typed_descriptor_validate(&STATE_BOUNDARY_DESCRIPTOR, &error),
        DATA_BIND_OK);

    check_equal(
        tbe_typed_descriptor_init(&STATE_BOUNDARY_DESCRIPTOR, &object, &error),
        DATA_BIND_OK);
    check_equal(object.value, 0u);
    check_equal(object.presence, 0u);
    check_equal(object.nulls, 0u);

    object.value = 42u;
    object.presence = 1u;
    object.nulls = 1u;
    check_equal(
        tbe_typed_descriptor_clear(&STATE_BOUNDARY_DESCRIPTOR, &object, &error),
        DATA_BIND_OK);
    check_equal(object.value, 0u);
    check_equal(object.presence, 0u);
    check_equal(object.nulls, 0u);
  }

  it("rejects overlapping presence and null host state") {
    TbeTypedType overlap = STATE_BOUNDARY_OVERLAY;
    DataBindError error = DATA_BIND_ERROR_INIT;

    overlap.null_offset = overlap.presence_offset;
    check_equal(tbe_typed_validate_descriptor(&overlap, &error),
                DATA_BIND_ERR_SCHEMA);
    check_contains(error.message, "overlap");
  }

  it("accepts nullable metadata but rejects value conversion until format lowering") {
    StateBoundary object = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    const uint8_t json[] = {'{', '}'};
    char *out = NULL;
    size_t out_len = 0u;

    check_null(tbe_typed_to_json(
        &STATE_BOUNDARY_OVERLAY, &object, &error));
    check_equal(error.code, DATA_BIND_ERR_SCHEMA);
    check_contains(error.message, "nullable");

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        tbe_typed_descriptor_parse(
            NULL, "StateBoundary", &STATE_BOUNDARY_DESCRIPTOR,
            DATA_BIND_FORMAT_JSON, json, sizeof(json), 0u, &object, &error),
        DATA_BIND_ERR_SCHEMA);
    check_contains(error.message, "nullable");

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        tbe_typed_descriptor_serialize(
            NULL, "StateBoundary", &STATE_BOUNDARY_DESCRIPTOR, &object,
            DATA_BIND_FORMAT_JSON, &out, &out_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(out);
    check_equal(out_len, 0u);
    check_contains(error.message, "nullable");
  }

}

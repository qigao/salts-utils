#include "data_bind_typed.h"
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

static DataBindTypedType boundary_type(const DataBindTypedField *fields, size_t count) {
  DataBindTypedType type = {
      .name = "Boundary", .size = sizeof(BoundaryStorage),
      .fields = fields, .field_count = count, .fixed_block_size = 8u};
  return type;
}

/* A one-byte input/output also proves schema errors take precedence over
 * truncation/capacity errors. Poisoned owning storage must never be inspected. */
static void expect_binary_rejection(const DataBindTypedType *type) {
  BoundaryStorage object;
  uint8_t before[sizeof(object)];
  const uint8_t input = BOUNDARY_SENTINEL;
  uint8_t output = BOUNDARY_SENTINEL;
  size_t out_len = sizeof(object);
  DataBindError error = DATA_BIND_ERROR_INIT;
  memset(&object, BOUNDARY_SENTINEL, sizeof(object));
  memcpy(before, &object, sizeof(object));

  check_equal(data_bind_typed_parse_binary(type, &input, sizeof(input), &object, &error),
              DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  check_equal(data_bind_typed_serialize_binary_into(type, &object, &output, sizeof(output),
                                             &out_len, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(out_len, 0u);
  check_equal(output, BOUNDARY_SENTINEL);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
}

/* Host-only APIs deliberately do not require a binary layout: text-only
 * descriptors remain valid. Only malformed host layouts reach this helper. */
static void expect_host_rejection(const DataBindTypedType *type) {
  BoundaryStorage object;
  uint8_t before[sizeof(object)];
  DataBindError error = DATA_BIND_ERROR_INIT;
  memset(&object, BOUNDARY_SENTINEL, sizeof(object));
  memcpy(before, &object, sizeof(object));

  check_equal(data_bind_typed_validate_descriptor(type, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(data_bind_typed_init(type, &object, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  data_bind_typed_clear(type, &object);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  expect_binary_rejection(type);
}

static void expect_descriptor_rejection(const DataBindTypedDescriptor *descriptor) {
  BoundaryStorage object;
  uint8_t before[sizeof(object)];
  const uint8_t input[] = {'{', '}'};
  DataBindError error = DATA_BIND_ERROR_INIT;
  memset(&object, BOUNDARY_SENTINEL, sizeof(object));
  memcpy(before, &object, sizeof(object));

  check_equal(data_bind_typed_descriptor_validate(descriptor, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(data_bind_typed_descriptor_init(descriptor, &object, &error), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  check_equal(data_bind_typed_descriptor_parse(NULL, "Boundary", descriptor,
                                         DATA_BIND_FORMAT_JSON, input,
                                         sizeof(input), 0u, &object, &error),
              DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
  check_equal(data_bind_typed_descriptor_clear(descriptor, &object, &error),
              DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&object, before, sizeof(object)), 0);
}

spec("typed descriptor boundary") {
  it("rejects owning overlap before initialization or cleanup touches storage") {
    const DataBindTypedField fields[] = {
        {.name = "a", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING},
        {.name = "b", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING}};
    DataBindTypedType type = boundary_type(fields, 2u);
    expect_host_rejection(&type);
  }

  it("rejects scalar then owner overlap independently of declaration order") {
    const DataBindTypedField fields[] = {
        {.name = "bits", .kind = DATA_BIND_TYPED_U32, .wire_kind = DATA_BIND_TYPED_U32},
        {.name = "owner", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING}};
    DataBindTypedType type = boundary_type(fields, 2u);
    expect_host_rejection(&type);
  }

  it("rejects presence overlapping owning storage before lifecycle operations") {
    const DataBindTypedField field = {
        .name = "owner", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING};
    DataBindTypedType type = boundary_type(&field, 1u);
    type.presence_size = 1u;
    expect_host_rejection(&type);
  }

  it("rejects owning fixed arrays overlapping scalar storage") {
    const DataBindTypedField fields[] = {
        {.name = "owners", .kind = DATA_BIND_TYPED_FIXED_ARRAY,
         .element_kind = DATA_BIND_TYPED_STRING, .element_wire_kind = DATA_BIND_TYPED_STRING,
         .element_size = sizeof(tstr), .fixed_count = 2u},
        {.name = "bits", .kind = DATA_BIND_TYPED_U32, .wire_kind = DATA_BIND_TYPED_U32,
         .offset = sizeof(tstr)}};
    DataBindTypedType type = boundary_type(fields, 2u);
    expect_host_rejection(&type);
  }

  it("rejects invalid nested ownership before touching the parent object") {
    const DataBindTypedField children[] = {
        {.name = "a", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING},
        {.name = "b", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING}};
    DataBindTypedType child = boundary_type(children, 2u);
    const DataBindTypedField field = {
        .name = "child", .kind = DATA_BIND_TYPED_OBJECT, .object_type = &child};
    DataBindTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects host offset addition overflow") {
    const DataBindTypedField field = {
        .name = "value", .kind = DATA_BIND_TYPED_U32, .wire_kind = DATA_BIND_TYPED_U32,
        .offset = SIZE_MAX - 1u};
    DataBindTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects host presence addition overflow") {
    DataBindTypedType type = boundary_type(NULL, 0u);
    type.presence_offset = SIZE_MAX - 1u;
    type.presence_size = sizeof(uint32_t);
    expect_host_rejection(&type);
  }

  it("rejects fixed array host extent multiplication overflow") {
    const DataBindTypedField field = {
        .name = "values", .kind = DATA_BIND_TYPED_FIXED_ARRAY,
        .element_kind = DATA_BIND_TYPED_U32, .element_wire_kind = DATA_BIND_TYPED_U32,
        .element_size = sizeof(uint32_t),
        .fixed_count = SIZE_MAX / sizeof(uint32_t) + 1u};
    DataBindTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects map key and scalar value overlap before lifecycle operations") {
    const DataBindTypedField field = {
        .name = "entries", .kind = DATA_BIND_TYPED_MAP,
        .element_size = sizeof(tstr), .map_entry_size = sizeof(tstr),
        .map_value_kind = DATA_BIND_TYPED_U32, .map_value_wire_kind = DATA_BIND_TYPED_U32};
    DataBindTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects map key interval addition overflow") {
    const DataBindTypedField field = {
        .name = "entries", .kind = DATA_BIND_TYPED_MAP,
        .element_size = SIZE_MAX, .map_entry_size = SIZE_MAX,
        .map_key_offset = SIZE_MAX - sizeof(tstr) + 1u,
        .map_value_kind = DATA_BIND_TYPED_U32, .map_value_wire_kind = DATA_BIND_TYPED_U32};
    DataBindTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects map value interval addition overflow") {
    const DataBindTypedField field = {
        .name = "entries", .kind = DATA_BIND_TYPED_MAP,
        .element_size = SIZE_MAX, .map_entry_size = SIZE_MAX,
        .map_value_offset = SIZE_MAX - 1u,
        .map_value_kind = DATA_BIND_TYPED_U32, .map_value_wire_kind = DATA_BIND_TYPED_U32};
    DataBindTypedType type = boundary_type(&field, 1u);
    expect_host_rejection(&type);
  }

  it("rejects oversized wire presence before decoding or writing") {
    DataBindTypedType type = boundary_type(NULL, 0u);
    type.fixed_block_size = 1u;
    type.presence_size = 8u;
    expect_binary_rejection(&type);
  }

  it("rejects overlapping wire fields before decoding or writing") {
    const DataBindTypedField fields[] = {
        {.name = "a", .kind = DATA_BIND_TYPED_U32, .wire_kind = DATA_BIND_TYPED_U32,
         .wire_offset = 0u, .wire_size = 4u, .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
        {.name = "b", .kind = DATA_BIND_TYPED_U32, .wire_kind = DATA_BIND_TYPED_U32,
         .offset = sizeof(uint32_t), .wire_offset = 2u, .wire_size = 4u,
         .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET}};
    DataBindTypedType type = boundary_type(fields, 2u);
    expect_binary_rejection(&type);
  }

  it("rejects wire fields overlapping presence before decoding or writing") {
    const DataBindTypedField field = {
        .name = "value", .kind = DATA_BIND_TYPED_U32, .wire_kind = DATA_BIND_TYPED_U32,
        .wire_size = 4u, .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET};
    DataBindTypedType type = boundary_type(&field, 1u);
    type.presence_offset = sizeof(uint32_t);
    type.presence_size = 1u;
    expect_binary_rejection(&type);
  }

  it("rejects wire size disagreement before decoding or writing") {
    const DataBindTypedField field = {
        .name = "value", .kind = DATA_BIND_TYPED_U32, .wire_kind = DATA_BIND_TYPED_U32,
        .wire_size = 1u, .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET};
    DataBindTypedType type = boundary_type(&field, 1u);
    expect_binary_rejection(&type);
  }

  it("rejects fixed wire interval addition overflow") {
    const DataBindTypedField field = {
        .name = "value", .kind = DATA_BIND_TYPED_U32, .wire_kind = DATA_BIND_TYPED_U32,
        .wire_offset = SIZE_MAX - 1u, .wire_size = 4u,
        .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET};
    DataBindTypedType type = boundary_type(&field, 1u);
    type.fixed_block_size = SIZE_MAX;
    expect_binary_rejection(&type);
  }

  it("rejects array wire extent overflow even when its host extent fits") {
    const DataBindTypedType child = {
        .name = "WideWireChild", .size = 1u,
        .fixed_block_size = SIZE_MAX / 2u + 1u};
    const DataBindTypedField field = {
        .name = "children", .kind = DATA_BIND_TYPED_FIXED_ARRAY,
        .element_kind = DATA_BIND_TYPED_OBJECT, .element_size = 1u,
        .fixed_count = 2u, .object_type = &child,
        .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET};
    DataBindTypedType type = boundary_type(&field, 1u);
    DataBindError error = DATA_BIND_ERROR_INIT;
    type.fixed_block_size = SIZE_MAX;
    check_equal(data_bind_typed_validate_descriptor(&type, &error), DATA_BIND_OK);
    expect_binary_rejection(&type);
  }

  it("keeps adjacent owning fields and host presence valid for text bindings") {
    typedef struct AdjacentOwners { tstr owners[2]; uint8_t presence; } AdjacentOwners;
    const DataBindTypedField fields[] = {
        {.name = "a", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING},
        {.name = "b", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING,
         .offset = sizeof(tstr)}};
    DataBindTypedType type = boundary_type(fields, 2u);
    AdjacentOwners object;
    DataBindError error = DATA_BIND_ERROR_INIT;
    type.size = sizeof(object);
    type.fixed_block_size = 0u;
    type.presence_offset = offsetof(AdjacentOwners, presence);
    type.presence_size = 1u;
    check_equal(data_bind_typed_validate_descriptor(&type, &error), DATA_BIND_OK);
    check_equal(data_bind_typed_init(&type, &object, &error), DATA_BIND_OK);
    data_bind_typed_clear(&type, &object);
  }

  it("keeps empty host intervals disjoint from owning storage") {
    const DataBindTypedField fields[] = {
        {.name = "empty", .kind = DATA_BIND_TYPED_FIXED_BYTES, .fixed_count = 0u},
        {.name = "owner", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING}};
    DataBindTypedType type = boundary_type(fields, 2u);
    BoundaryStorage object;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(data_bind_typed_validate_descriptor(&type, &error), DATA_BIND_OK);
    check_equal(data_bind_typed_init(&type, &object, &error), DATA_BIND_OK);
    data_bind_typed_clear(&type, &object);
  }

  it("requires an ABI-v2 descriptor with one canonical Struct graph") {
    const DataBindTypedField overlay_field = {
        .name = "value", .kind = DATA_BIND_TYPED_U8, .wire_kind = DATA_BIND_TYPED_U8};
    DataBindTypedType overlay = boundary_type(NULL, 0u);
    DataBindTypedType mismatched_overlay = boundary_type(&overlay_field, 1u);
    DataBindTypedDescriptor descriptor =
        DATA_BIND_TYPED_DESCRIPTOR_INIT(&overlay, &BOUNDARY_DATA);
    DataBindTypedDescriptor rejected;
    cmeta_data_desc bad_data;
    DataBindError error = DATA_BIND_ERROR_INIT;

    overlay.fixed_block_size = 0u;
    mismatched_overlay.fixed_block_size = 0u;
    check_equal(data_bind_typed_descriptor_validate(&descriptor, &error), DATA_BIND_OK);

    rejected = descriptor;
    rejected.struct_size = offsetof(DataBindTypedDescriptor, native_data);
    expect_descriptor_rejection(&rejected);

    rejected = descriptor;
    rejected.abi_version = 1u;
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
}

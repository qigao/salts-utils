#include "data_bind_typed.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

spec("typed descriptor safety") {
  it("rejects a presence bitmap larger than the fixed wire block") {
    typedef struct PresenceOnly {
      uint8_t presence[8];
    } PresenceOnly;

    static const DataBindTypedType type = {
        .name = "PresenceOnly",
        .size = sizeof(PresenceOnly),
        .fields = NULL,
        .field_count = 0,
        .fixed_block_size = 1,
        .presence_offset = offsetof(PresenceOnly, presence),
        .presence_size = sizeof(((PresenceOnly *)0)->presence),
    };
    PresenceOnly object = {{0}};
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t out_len = 99u;

    check_equal(data_bind_typed_serialize_binary_into(&type, &object, NULL, 0u, &out_len, &error),
                DATA_BIND_ERR_SCHEMA);
    check_equal(out_len, 0u);
  }

  it("rejects overlapping owning host fields") {
    typedef struct HostOverlap {
      tstr value;
    } HostOverlap;

    static const DataBindTypedField fields[] = {
        {.name = "first", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING,
         .offset = offsetof(HostOverlap, value)},
        {.name = "second", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING,
         .offset = offsetof(HostOverlap, value)},
    };
    static const DataBindTypedType type = {
        .name = "HostOverlap",
        .size = sizeof(HostOverlap),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
    };
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_typed_validate_descriptor(&type, &error), DATA_BIND_ERR_SCHEMA);
  }

  it("rejects owning host storage overlapping a scalar field") {
    typedef union HostOwnerScalarOverlap {
      tstr text;
      uint64_t bits;
    } HostOwnerScalarOverlap;

    static const DataBindTypedField fields[] = {
        {.name = "text", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING,
         .offset = offsetof(HostOwnerScalarOverlap, text)},
        {.name = "bits", .kind = DATA_BIND_TYPED_U64, .wire_kind = DATA_BIND_TYPED_U64,
         .offset = offsetof(HostOwnerScalarOverlap, bits)},
    };
    static const DataBindTypedType type = {
        .name = "HostOwnerScalarOverlap",
        .size = sizeof(HostOwnerScalarOverlap),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
    };
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_typed_validate_descriptor(&type, &error), DATA_BIND_ERR_SCHEMA);
  }

  it("rejects a presence bitmap overlapping owning host storage") {
    typedef struct PresenceHostOverlap {
      tstr value;
    } PresenceHostOverlap;

    static const DataBindTypedField fields[] = {
        {.name = "value",
         .kind = DATA_BIND_TYPED_STRING,
         .wire_kind = DATA_BIND_TYPED_STRING,
         .offset = offsetof(PresenceHostOverlap, value),
         .optional_bit = 0,
         .flags = DATA_BIND_TYPED_FIELD_OPTIONAL},
    };
    static const DataBindTypedType type = {
        .name = "PresenceHostOverlap",
        .size = sizeof(PresenceHostOverlap),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .presence_offset = offsetof(PresenceHostOverlap, value),
        .presence_size = 1,
    };
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_typed_validate_descriptor(&type, &error), DATA_BIND_ERR_SCHEMA);
  }

  it("rejects overlapping map key and value storage") {
    typedef struct MapEntry {
      tstr key;
    } MapEntry;
    typedef struct MapOwner {
      vec_t entries;
    } MapOwner;

    static const DataBindTypedField fields[] = {
        {.name = "entries",
         .kind = DATA_BIND_TYPED_MAP,
         .wire_kind = DATA_BIND_TYPED_MAP,
         .offset = offsetof(MapOwner, entries),
         .element_size = sizeof(MapEntry),
         .map_entry_size = sizeof(MapEntry),
         .map_key_offset = offsetof(MapEntry, key),
         .map_value_offset = offsetof(MapEntry, key),
         .map_value_kind = DATA_BIND_TYPED_STRING,
         .map_value_wire_kind = DATA_BIND_TYPED_STRING},
    };
    static const DataBindTypedType type = {
        .name = "MapOwner",
        .size = sizeof(MapOwner),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
    };
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_typed_validate_descriptor(&type, &error), DATA_BIND_ERR_SCHEMA);
  }

  it("rejects map key storage overlapping a scalar value") {
    typedef union MapEntry {
      tstr key;
      uint32_t value;
    } MapEntry;
    typedef struct MapOwner {
      vec_t entries;
    } MapOwner;

    static const DataBindTypedField fields[] = {
        {.name = "entries",
         .kind = DATA_BIND_TYPED_MAP,
         .wire_kind = DATA_BIND_TYPED_MAP,
         .offset = offsetof(MapOwner, entries),
         .element_size = sizeof(MapEntry),
         .map_entry_size = sizeof(MapEntry),
         .map_key_offset = offsetof(MapEntry, key),
         .map_value_offset = offsetof(MapEntry, value),
         .map_value_kind = DATA_BIND_TYPED_U32,
         .map_value_wire_kind = DATA_BIND_TYPED_U32},
    };
    static const DataBindTypedType type = {
        .name = "MapOwnerScalarOverlap",
        .size = sizeof(MapOwner),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
    };
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_typed_validate_descriptor(&type, &error), DATA_BIND_ERR_SCHEMA);
  }

  it("rejects overlapping fixed wire field ranges") {
    typedef struct WireOverlap {
      uint32_t first;
      uint32_t second;
    } WireOverlap;

    static const DataBindTypedField fields[] = {
        {.name = "first",
         .kind = DATA_BIND_TYPED_U32,
         .wire_kind = DATA_BIND_TYPED_U32,
         .offset = offsetof(WireOverlap, first),
         .wire_offset = 0,
         .wire_size = 4,
         .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
        {.name = "second",
         .kind = DATA_BIND_TYPED_U32,
         .wire_kind = DATA_BIND_TYPED_U32,
         .offset = offsetof(WireOverlap, second),
         .wire_offset = 2,
         .wire_size = 4,
         .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
    };
    static const DataBindTypedType type = {
        .name = "WireOverlap",
        .size = sizeof(WireOverlap),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .fixed_block_size = 6,
    };
    WireOverlap object = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t out_len = 99u;

    check_equal(data_bind_typed_serialize_binary_into(&type, &object, NULL, 0u, &out_len, &error),
                DATA_BIND_ERR_SCHEMA);
    check_equal(out_len, 0u);
  }

  it("rejects a fixed wire field overlapping the presence range") {
    typedef struct PresenceWireOverlap {
      uint32_t value;
      uint8_t presence[1];
    } PresenceWireOverlap;

    static const DataBindTypedField fields[] = {
        {.name = "value",
         .kind = DATA_BIND_TYPED_U32,
         .wire_kind = DATA_BIND_TYPED_U32,
         .offset = offsetof(PresenceWireOverlap, value),
         .wire_offset = 0,
         .wire_size = 4,
         .optional_bit = 0,
         .flags = DATA_BIND_TYPED_FIELD_OPTIONAL | DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
    };
    static const DataBindTypedType type = {
        .name = "PresenceWireOverlap",
        .size = sizeof(PresenceWireOverlap),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .fixed_block_size = 4,
        .presence_offset = offsetof(PresenceWireOverlap, presence),
        .presence_size = sizeof(((PresenceWireOverlap *)0)->presence),
    };
    PresenceWireOverlap object = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t out_len = 99u;

    check_equal(data_bind_typed_serialize_binary_into(&type, &object, NULL, 0u, &out_len, &error),
                DATA_BIND_ERR_SCHEMA);
    check_equal(out_len, 0u);
  }

  it("rejects a declared wire size that disagrees with the field kind") {
    typedef struct WireSizeMismatch {
      uint32_t value;
    } WireSizeMismatch;

    static const DataBindTypedField fields[] = {
        {.name = "value",
         .kind = DATA_BIND_TYPED_U32,
         .wire_kind = DATA_BIND_TYPED_U32,
         .offset = offsetof(WireSizeMismatch, value),
         .wire_offset = 0,
         .wire_size = 1,
         .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
    };
    static const DataBindTypedType type = {
        .name = "WireSizeMismatch",
        .size = sizeof(WireSizeMismatch),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .fixed_block_size = 4,
    };
    WireSizeMismatch object = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t out_len = 99u;

    check_equal(data_bind_typed_serialize_binary_into(&type, &object, NULL, 0u, &out_len, &error),
                DATA_BIND_ERR_SCHEMA);
    check_equal(out_len, 0u);
  }

  it("keeps adjacent fixed wire ranges valid") {
    typedef struct AdjacentWire {
      uint16_t first;
      uint16_t second;
    } AdjacentWire;

    static const DataBindTypedField fields[] = {
        {.name = "first",
         .kind = DATA_BIND_TYPED_U16,
         .wire_kind = DATA_BIND_TYPED_U16,
         .offset = offsetof(AdjacentWire, first),
         .wire_offset = 0,
         .wire_size = 2,
         .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
        {.name = "second",
         .kind = DATA_BIND_TYPED_U16,
         .wire_kind = DATA_BIND_TYPED_U16,
         .offset = offsetof(AdjacentWire, second),
         .wire_offset = 2,
         .wire_size = 2,
         .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
    };
    static const DataBindTypedType type = {
        .name = "AdjacentWire",
        .size = sizeof(AdjacentWire),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .fixed_block_size = 4,
    };
    AdjacentWire object = {0};
    uint8_t output[4] = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t out_len = 0u;

    check_equal(data_bind_typed_serialize_binary_into(&type, &object, output, sizeof(output), &out_len,
                                                 &error),
                DATA_BIND_OK);
    check_equal(out_len, sizeof(output));
  }
}

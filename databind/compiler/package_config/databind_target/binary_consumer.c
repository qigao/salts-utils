#include "installed_message_native.h"
#include <data_bind_message_plan.h>
#include <cstl/byte_buffer.h>
#include <tinytest.h>

#include <stdint.h>
#include <string.h>

enum {
  BINARY_INSTALLED_REPEATS = 40u, BINARY_SENTINEL = 0x7fu,
  BINARY_PAYLOAD_OFFSET = 8u, BINARY_PAYLOAD_BYTES = 3u
};
static const uint32_t BINARY_ID = UINT32_C(0x10203040);
static const uint8_t BINARY_WIRE[] = {
    0x40, 0x30, 0x20, 0x10, 3, 0, 0, 0, 'A', 0, 'B'};
static DataBind *codec;
static DataBindError error;
static BinaryOwned_t source, decoded;
static DataBindObject *dynamic;
static uint8_t *generic_wire;

spec("Installed generated Binary ownership and wire contract") {
  before_each() {
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    dynamic = NULL;
    generic_wire = NULL;
    BinaryOwned_init(&source);
    BinaryOwned_init(&decoded);
    check_equal(InstalledMessage_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(BinaryOwned_from_bin(codec, &source, BINARY_WIRE,
                                    sizeof(BINARY_WIRE), &error), DATA_BIND_OK);
  }
  after_each() {
    data_bind_binary_free(generic_wire);
    data_bind_object_free(dynamic);
    BinaryOwned_clear(&decoded);
    BinaryOwned_clear(&source);
    data_bind_free(codec);
    codec = NULL;
  }

  it("matches literal and generic bytes and rejects short output before publication") {
    uint8_t generated[sizeof(BINARY_WIRE)] = {0};
    uint8_t short_output[sizeof(BINARY_WIRE) - 1u];
    size_t generated_length = 0u, generic_length = 0u, required = 0u;
    memset(short_output, BINARY_SENTINEL, sizeof(short_output));
    check_equal(source.id, BINARY_ID);
    check_equal(stl_byte_buffer_size(&source.payload), (size_t)BINARY_PAYLOAD_BYTES);
    check_equal(stl_byte_buffer_data_const(&source.payload)[1], (uint8_t)0u);
    check_equal(BinaryOwned_to_bin_into(codec, &source, generated, sizeof(generated),
                                       &generated_length, &error), DATA_BIND_OK);
    check_equal(generated_length, sizeof(BINARY_WIRE));
    check_equal(generated, BINARY_WIRE, sizeof(BINARY_WIRE));
    check_equal(data_bind_object_from_bin(codec, "BinaryOwned", BINARY_WIRE,
        sizeof(BINARY_WIRE), &dynamic, &error), DATA_BIND_OK);
    check_equal(data_bind_object_serialize_bin(codec, dynamic, &generic_wire,
                                              &generic_length, &error), DATA_BIND_OK);
    check_equal(generic_length, sizeof(BINARY_WIRE));
    check_equal(generic_wire, BINARY_WIRE, sizeof(BINARY_WIRE));
    check_equal(BinaryOwned_to_bin_into(codec, &source, short_output,
        sizeof(short_output), &required, &error), DATA_BIND_ERR_LIMIT);
    check_equal(required, sizeof(BINARY_WIRE));
    for (size_t i = 0u; i < sizeof(short_output); ++i)
      check_equal(short_output[i], (uint8_t)BINARY_SENTINEL);
  }

  it("retains independent native owners after input reuse and codec destruction") {
    const DataBindMessagePlan *first = NULL, *warm = NULL;
    uint8_t wire[sizeof(BINARY_WIRE)];
    check_equal(data_bind_message_plan_acquire_generated(
        codec, BinaryOwned_native_artifact(), &first, &error), DATA_BIND_OK);
    for (size_t i = 0u; i < BINARY_INSTALLED_REPEATS; ++i) {
      BinaryOwned_clear(&decoded);
      memcpy(wire, BINARY_WIRE, sizeof(wire));
      check_equal(BinaryOwned_from_bin(codec, &decoded, wire, sizeof(wire), &error),
                  DATA_BIND_OK);
      memset(wire, BINARY_SENTINEL, sizeof(wire));
      check_equal(decoded.id, BINARY_ID);
      check_equal(stl_byte_buffer_size(&decoded.payload), (size_t)BINARY_PAYLOAD_BYTES);
      check_equal(stl_byte_buffer_data_const(&decoded.payload), BINARY_WIRE + BINARY_PAYLOAD_OFFSET,
                  (size_t)BINARY_PAYLOAD_BYTES);
      check_true(stl_byte_buffer_data_const(&decoded.payload) !=
                 stl_byte_buffer_data_const(&source.payload));
    }
    check_equal(data_bind_message_plan_acquire_generated(
        codec, BinaryOwned_native_artifact(), &warm, &error), DATA_BIND_OK);
    check_true(first == warm);
    data_bind_free(codec);
    codec = NULL;
    BinaryOwned_clear(&source);
    check_equal(stl_byte_buffer_data_const(&decoded.payload), BINARY_WIRE + BINARY_PAYLOAD_OFFSET,
                (size_t)BINARY_PAYLOAD_BYTES);
    BinaryOwned_clear(&decoded);
    BinaryOwned_clear(&decoded);
    check_true(stl_byte_buffer_data_const(&decoded.payload) == NULL);
    check_equal(stl_byte_buffer_size(&decoded.payload), (size_t)0u);
  }
}

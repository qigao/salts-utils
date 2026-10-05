#include "binary_admission_generated.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

enum { GENERATED_WIRE_CAPACITY = 128, GENERATED_WIRE_SENTINEL = 0xa5 };

spec("generated canonical Binary admission") {
  it("round-trips the original nested Header wire through the canonical provider") {
    static const uint8_t expected[] = {
        3u, 0x78u, 0x56u, 0x34u, 0x12u,
        0xefu, 0xcdu, 0xabu, 0x89u, 0x67u, 0x45u, 0x23u, 0x01u,
        73u, 0u, 0u, 0u};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    Heartbeat_t source = {0};
    Heartbeat_t decoded = {0};
    uint8_t wire[GENERATED_WIRE_CAPACITY] = {0};
    uint8_t expected_wire[GENERATED_WIRE_CAPACITY] = {0};
    uint8_t *allocated_wire = NULL;
    size_t length = 0u;
    DataBindStatus status = Session_codec_create(&codec, &error);
    check_equal(status, DATA_BIND_OK);
    Heartbeat_init(&source);
    Heartbeat_init(&decoded);
    source.header.type = 3u;
    source.header.seq_num = UINT32_C(0x12345678);
    source.header.timestamp = UINT64_C(0x0123456789abcdef);
    source.load = 73u;
    decoded.header.seq_num = source.header.seq_num;
    memset(wire, GENERATED_WIRE_SENTINEL, sizeof(wire));
    memset(expected_wire, GENERATED_WIRE_SENTINEL, sizeof(expected_wire));
    if (codec != NULL) {
      status = Heartbeat_to_bin(codec, &source, &allocated_wire, &length, &error);
      check_equal(status, DATA_BIND_OK);
      check_equal(length, sizeof(expected));
      check_equal(allocated_wire, expected, sizeof(expected));
      status = Heartbeat_to_bin_into(codec, &source, wire, sizeof(wire), &length, &error);
      check_equal(status, DATA_BIND_OK);
      check_equal(length, sizeof(expected));
      check_equal(wire, expected, sizeof(expected));
      status = Heartbeat_from_bin(codec, &decoded, wire, length, &error);
      check_equal(status, DATA_BIND_OK);
      check_equal(decoded.header.type, source.header.type);
      check_equal(decoded.header.seq_num, source.header.seq_num);
      check_equal(decoded.header.timestamp, source.header.timestamp);
      check_equal(decoded.load, source.load);
      check_equal(Heartbeat_from_bin(codec, &decoded, wire, length - 1u, &error),
                  DATA_BIND_ERR_PARSE);
      check_equal(decoded.header.seq_num, source.header.seq_num);
      check_equal(decoded.load, source.load);
      memset(wire, GENERATED_WIRE_SENTINEL, sizeof(wire));
      status = Heartbeat_to_bin_into(codec, &source, wire, sizeof(expected) - 1u, &length, &error);
      check_equal(status, DATA_BIND_ERR_LIMIT);
      check_equal(length, (size_t)0u);
      check_equal(memcmp(wire, expected_wire, sizeof(wire)), 0);
    }
    data_bind_binary_free(allocated_wire);
    Heartbeat_clear(&decoded);
    Heartbeat_clear(&source);
    data_bind_free(codec);
  }

  it("fails closed for generated mixed fixed-bytes and nested Binary layouts") {
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    LoginMessage_t source = {0};
    LoginMessage_t decoded = {0};
    uint8_t wire[GENERATED_WIRE_CAPACITY] = {0};
    uint8_t expected_wire[GENERATED_WIRE_CAPACITY] = {0};
    uint8_t *allocated_wire = NULL;
    size_t length = 0u;
    DataBindStatus status = Session_codec_create(&codec, &error);
    check_equal(status, DATA_BIND_OK);
    LoginMessage_init(&source);
    LoginMessage_init(&decoded);
    source.header.type = 1u;
    source.header.seq_num = 37u;
    source.header.timestamp = 99u;
    memset(source.pass_hash, 0x5a, sizeof(source.pass_hash));
    source.username = tstr_dup("binary-admission");
    check_not_null(source.username);
    decoded.header.seq_num = source.header.seq_num;
    memset(wire, GENERATED_WIRE_SENTINEL, sizeof(wire));
    memset(expected_wire, GENERATED_WIRE_SENTINEL, sizeof(expected_wire));
    if (codec != NULL && source.username != NULL) {
      status = LoginMessage_from_bin(codec, &decoded, wire, sizeof(wire), &error);
      check_equal(status, DATA_BIND_ERR_SCHEMA);
      check_equal(decoded.header.seq_num, source.header.seq_num);
      length = sizeof(wire);
      status = LoginMessage_to_bin(codec, &source, &allocated_wire, &length, &error);
      check_equal(status, DATA_BIND_ERR_SCHEMA);
      check_null(allocated_wire);
      check_equal(length, (size_t)0u);
      status = LoginMessage_to_bin_into(codec, &source, wire, sizeof(wire), &length, &error);
      check_equal(status, DATA_BIND_ERR_SCHEMA);
      check_equal(length, (size_t)0u);
      check_equal(memcmp(wire, expected_wire, sizeof(wire)), 0);
    }
    LoginMessage_clear(&decoded);
    LoginMessage_clear(&source);
    check_null(source.username);
    check_equal(source.header.seq_num, 0u);
    check_equal(source.pass_hash, (uint8_t[sizeof(source.pass_hash)]){0}, sizeof(source.pass_hash));
    LoginMessage_clear(&source);
    data_bind_free(codec);
  }
}

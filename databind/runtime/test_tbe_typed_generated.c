#include "tbe30_generated.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

enum { GENERATED_WIRE_CAPACITY = 128 };

spec("generated typed descriptor compatibility") {
  it("round trips compiled generated nested fixed descriptors") {
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    Heartbeat_t source = {0};
    Heartbeat_t decoded = {0};
    uint8_t wire[GENERATED_WIRE_CAPACITY] = {0};
    size_t length = 0u;
    DataBindStatus status = Session_codec_create(&codec, &error);
    check_equal(status, DATA_BIND_OK);
    Heartbeat_init(&source);
    Heartbeat_init(&decoded);
    source.header.type = 3u;
    source.header.seq_num = UINT32_C(0x12345678);
    source.header.timestamp = UINT64_C(0x0123456789abcdef);
    source.load = 73u;
    if (codec != NULL) {
      status = Heartbeat_to_bin_into(&source, wire, sizeof(wire), &length, &error);
      check_equal(status, DATA_BIND_OK);
      if (status == DATA_BIND_OK) {
        check_equal(Heartbeat_from_bin(codec, &decoded, wire, length, &error), DATA_BIND_OK);
        check_equal(decoded.header.type, source.header.type);
        check_equal(decoded.header.seq_num, source.header.seq_num);
        check_equal(decoded.header.timestamp, source.header.timestamp);
        check_equal(decoded.load, source.load);
      }
    }
    Heartbeat_clear(&decoded);
    Heartbeat_clear(&source);
    data_bind_free(codec);
  }

  it("round trips compiled generated fixed bytes and an owning string tail") {
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    LoginMessage_t source = {0};
    LoginMessage_t decoded = {0};
    uint8_t wire[GENERATED_WIRE_CAPACITY] = {0};
    size_t length = 0u;
    DataBindStatus status = Session_codec_create(&codec, &error);
    check_equal(status, DATA_BIND_OK);
    LoginMessage_init(&source);
    LoginMessage_init(&decoded);
    source.header.type = 1u;
    source.header.seq_num = 37u;
    source.header.timestamp = 99u;
    memset(source.pass_hash, 0x5a, sizeof(source.pass_hash));
    source.username = tstr_dup("descriptor-boundary");
    check_not_null(source.username);
    if (codec != NULL && source.username != NULL) {
      status = LoginMessage_to_bin_into(&source, wire, sizeof(wire), &length, &error);
      check_equal(status, DATA_BIND_OK);
      if (status == DATA_BIND_OK) {
        check_equal(LoginMessage_from_bin(codec, &decoded, wire, length, &error), DATA_BIND_OK);
        check_equal(decoded.header.type, source.header.type);
        check_equal(decoded.header.seq_num, source.header.seq_num);
        check_equal(decoded.header.timestamp, source.header.timestamp);
        check_equal(memcmp(decoded.pass_hash, source.pass_hash, sizeof(source.pass_hash)), 0);
        check_not_null(decoded.username);
        if (decoded.username != NULL)
          check_equal(strcmp(decoded.username, source.username), 0);
      }
    }
    LoginMessage_clear(&decoded);
    LoginMessage_clear(&source);
    data_bind_free(codec);
  }
}

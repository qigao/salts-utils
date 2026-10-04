#include "tbe30_generated.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

enum { GENERATED_WIRE_CAPACITY = 128 };

spec("generated typed descriptor compatibility") {
  it("fails closed for generated nested Binary layouts without canonical admission") {
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
      status = Heartbeat_to_bin_into(codec, &source, wire, sizeof(wire), &length, &error);
      check_equal(status, DATA_BIND_ERR_SCHEMA);
      check_equal(length, (size_t)0u);
    }
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
      status = LoginMessage_to_bin_into(codec, &source, wire, sizeof(wire), &length, &error);
      check_equal(status, DATA_BIND_ERR_SCHEMA);
      check_equal(length, (size_t)0u);
    }
    LoginMessage_clear(&decoded);
    LoginMessage_clear(&source);
    data_bind_free(codec);
  }
}

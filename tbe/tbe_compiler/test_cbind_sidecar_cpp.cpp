#include "cbind_sidecar.h"

#include "tinytest.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

static_assert(std::is_same<decltype(CBindEnvelope_t::sint64), std::int64_t>::value,
              "generated int64 fields must stay fixed-width on LLP64");
static_assert(std::is_same<decltype(CBindEnvelope_t::uint64_value), std::uint64_t>::value,
              "generated uint64 fields must stay fixed-width on LLP64");
#if defined(_WIN32)
static_assert(sizeof(long) == 4u, "this consumer must exercise the Windows LLP64 ABI");
#endif

namespace {

constexpr std::size_t kScratchBytes = 4u;
constexpr std::size_t kMaxDepth = 2u;
constexpr std::size_t kMaxBufferBytes = 36u;

cserde_token structural(cserde_token_kind kind) {
  cserde_token token{};
  token.kind = kind;
  return token;
}

cserde_token text(cserde_token_kind kind, const char *value) {
  cserde_token token{};
  token.kind = kind;
  token.value.slice.data = reinterpret_cast<const unsigned char *>(value);
  token.value.slice.size = std::strlen(value);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

cserde_token boolean(bool value) {
  cserde_token token{};
  token.kind = CSERDE_BOOL;
  token.value.boolean = value;
  return token;
}

cserde_token sint(std::int64_t value) {
  cserde_token token{};
  token.kind = CSERDE_SINT;
  token.value.sint = value;
  return token;
}

cserde_token uint(std::uint64_t value) {
  cserde_token token{};
  token.kind = CSERDE_UINT;
  token.value.uint = value;
  return token;
}

cserde_token floating(double value) {
  cserde_token token{};
  token.kind = CSERDE_FLOAT;
  token.value.floating = value;
  return token;
}

struct ReaderState {
  const cserde_token *tokens;
  std::size_t count;
  std::size_t position;
};

cserde_status next_token(void *opaque, cserde_token *out) {
  auto *state = static_cast<ReaderState *>(opaque);
  if (state == nullptr || out == nullptr || state->position >= state->count)
    return CSERDE_SOURCE_ERROR;
  *out = state->tokens[state->position++];
  return CSERDE_OK;
}

const cserde_reader_ops kReaderOps = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION, next_token};

}  // namespace

spec("generated CBind sidecar C++ linkage") {
  it("exposes struct and enum descriptors through the generated public header") {
    const cmeta_data_desc *descriptor = CBindEnvelope_cbind_data();
    const cmeta_data_desc *enum_descriptor = CBindState_cbind_data();

    check_not_null(descriptor);
    check_true(cmeta_data_desc_valid(descriptor));
    check_equal(descriptor->storage_type->size, sizeof(CBindEnvelope_t));
    check_not_null(enum_descriptor);
    check_true(cmeta_data_desc_valid(enum_descriptor));
    check_equal(enum_descriptor->kind, CMETA_DATA_ENUM);
    check_equal(enum_descriptor->storage_type->size, sizeof(std::uint16_t));
  }

  it("decodes fixed-width scalars uppercase UUID and enum text through from_cserde") {
    const cserde_token tokens[] = {
        structural(CSERDE_MAP_BEGIN), text(CSERDE_STRING, "header"),
        structural(CSERDE_MAP_BEGIN), text(CSERDE_STRING, "sequence"), sint(-17),
        structural(CSERDE_MAP_END), text(CSERDE_STRING, "eventId"), sint(42),
        text(CSERDE_STRING, "enabled"), boolean(true), text(CSERDE_STRING, "sint8"),
        sint(INT8_MIN), text(CSERDE_STRING, "uint8_value"), uint(UINT8_MAX),
        text(CSERDE_STRING, "sint16"), sint(INT16_MIN),
        text(CSERDE_STRING, "uint16_value"), uint(UINT16_MAX),
        text(CSERDE_STRING, "sint32"), sint(INT32_MIN),
        text(CSERDE_STRING, "uint32_value"), uint(UINT32_MAX),
        text(CSERDE_STRING, "sint64"), sint(INT64_MIN),
        text(CSERDE_STRING, "uint64_value"), uint(UINT64_MAX),
        text(CSERDE_STRING, "real32"), floating(1.25),
        text(CSERDE_STRING, "real64"), floating(3.5), text(CSERDE_STRING, "note"),
        text(CSERDE_STRING, "owned"), text(CSERDE_STRING, "request_id"),
        text(CSERDE_STRING, "00112233-4455-6677-8899-AABBCCDDEEFF"),
        text(CSERDE_STRING, "state"), text(CSERDE_STRING, "Ready"),
        text(CSERDE_STRING, "default_state"), sint(3), structural(CSERDE_MAP_END)};
    std::array<unsigned char, kScratchBytes> scratch{};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch.data(), scratch.size(), kMaxDepth, 0u, kMaxBufferBytes);
    cbind_error error = CBIND_ERROR_INIT;
    ReaderState state{tokens, sizeof(tokens) / sizeof(tokens[0]), 0u};
    cserde_reader reader{};
    CBindEnvelope_t envelope{};
    turbo_uuid_t expected_uuid{};

    check_equal(cserde_reader_init(&reader, &kReaderOps, &state), CSERDE_OK);
    check_equal(CBindEnvelope_from_cserde(&context, &reader, &envelope, &error), CBIND_OK);
    check_equal(envelope.header.sequence, -17);
    check_equal(envelope.event_id, 42);
    check_true(envelope.enabled != 0);
    check_true(envelope.sint64 == INT64_MIN);
    check_true(envelope.uint64_value == UINT64_MAX);
    check_true(envelope.note != nullptr);
    check_equal(turbo_uuid_parse("00112233-4455-6677-8899-AABBCCDDEEFF", &expected_uuid), 0);
    check_true(turbo_uuid_equal(&envelope.request_id, &expected_uuid));
    check_equal(envelope.state, CBindState_Ready);
    check_equal(envelope.default_state, CBindDefaultState_Active);
    tstr_freep(&envelope.note);
  }
}

#include "cbind_standalone.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

static_assert(std::is_same<decltype(CBindStandaloneEnvelope_t::min_value), std::int64_t>::value,
              "standalone int64 storage must remain fixed-width on LLP64");
static_assert(std::is_same<decltype(CBindStandaloneEnvelope_t::max_value), std::uint64_t>::value,
              "standalone uint64 storage must remain fixed-width on LLP64");
#if defined(_WIN32)
static_assert(sizeof(long) == 4u, "this standalone consumer must exercise Windows LLP64");
#endif

namespace {

cserde_token structural(cserde_token_kind kind) {
  cserde_token token{};
  token.kind = kind;
  return token;
}

cserde_token text(const char *value) {
  cserde_token token{};
  token.kind = CSERDE_STRING;
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

int main() {
  const cserde_token tokens[] = {
      structural(CSERDE_MAP_BEGIN), text("details"), structural(CSERDE_MAP_BEGIN),
      text("sequence"), sint(-17), structural(CSERDE_MAP_END), text("eventId"), sint(42),
      text("enabled"), boolean(true), text("min_value"), sint(INT64_MIN), text("max_value"),
      uint(UINT64_MAX), text("label"), text("owned"), text("request_id"),
      text("00112233-4455-6677-8899-AABBCCDDEEFF"), text("state"), text("Ready"),
      structural(CSERDE_MAP_END)};
  std::array<unsigned char, 2u> scratch{};
  cbind_context context =
      CBIND_CONTEXT_WITH_BUFFERS_INIT(scratch.data(), scratch.size(), 2u, 0u, 36u);
  cbind_error error = CBIND_ERROR_INIT;
  ReaderState state{tokens, sizeof(tokens) / sizeof(tokens[0]), 0u};
  cserde_reader reader{};
  CBindStandaloneEnvelope_t envelope{};
  turbo_uuid_t expected_uuid{};
  const cmeta_data_desc *descriptor = CBindStandaloneEnvelope_cbind_data();

  if (!cmeta_data_desc_valid(descriptor) ||
      !cmeta_data_desc_valid(CBindStandaloneState_cbind_data()))
    return 1;
  if (cserde_reader_init(&reader, &kReaderOps, &state) != CSERDE_OK) return 2;
  if (CBindStandaloneEnvelope_from_cserde(&context, &reader, &envelope, &error) != CBIND_OK)
    return 3;
  if (envelope.details.sequence != -17 || envelope.event_id != 42 || envelope.enabled == 0 ||
      envelope.min_value != INT64_MIN || envelope.max_value != UINT64_MAX ||
      envelope.label == nullptr || envelope.state != CBindStandaloneState_Ready)
    return 4;
  if (turbo_uuid_parse("00112233-4455-6677-8899-AABBCCDDEEFF", &expected_uuid) != 0 ||
      !turbo_uuid_equal(&envelope.request_id, &expected_uuid))
    return 5;
  tstr_freep(&envelope.label);
  return 0;
}

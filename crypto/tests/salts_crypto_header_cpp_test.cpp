#include <salts/crypto.h>
#include <cmeta_crypto.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <cstring>

static_assert(SALTS_CRYPTO_ED448_PRIVATE_KEY_SIZE == 57U);
static_assert(SALTS_CRYPTO_ED448_PUBLIC_KEY_SIZE == 57U);
static_assert(SALTS_CRYPTO_ED448_SIGNATURE_SIZE == 114U);
static_assert(SALTS_CRYPTO_SHA256_DIGEST_SIZE == 32U);
static_assert(SALTS_CRYPTO_ED25519_PRIVATE_KEY_SIZE == 32U);
static_assert(SALTS_CRYPTO_ED25519_SIGNATURE_SIZE == 64U);
static_assert(
    std::is_same_v<decltype(&salts_crypto_ed448_verify),
                   int (*)(const std::uint8_t *, const void *, std::size_t, const std::uint8_t *)>);
static_assert(std::is_same_v<decltype(&salts_crypto_sha256_update),
                             int (*)(salts_crypto_sha256_ctx_t *, const void *, std::size_t)>);

int main() {
  std::uint8_t libecc_digest[32], gmssl_digest[32];
  if (salts_crypto_sha256("abc", 3, libecc_digest) != SALTS_CRYPTO_OK ||
      cmeta_sha256("abc", 3, gmssl_digest) != 0) return 1;
  return std::memcmp(libecc_digest, gmssl_digest, sizeof(libecc_digest)) == 0 ? 0 : 1;
}

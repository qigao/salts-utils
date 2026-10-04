#include <salts/crypto.h>

#include "crypto_internal.h"

#include <limits.h>
#include <string.h>

#include <libecc/hash/sha256.h>

typedef struct salts_crypto_sha256_impl {
  sha256_context native;
  int initialized;
} salts_crypto_sha256_impl;

_Static_assert(sizeof(salts_crypto_sha256_impl) <= SALTS_CRYPTO_SHA256_CONTEXT_SIZE,
               "SALTS_CRYPTO_SHA256_CONTEXT_SIZE is too small");
_Static_assert(_Alignof(salts_crypto_sha256_impl) <= _Alignof(salts_crypto_sha256_ctx_t),
               "salts_crypto_sha256_ctx_t alignment is insufficient");

static salts_crypto_sha256_impl *salts_crypto_sha256_impl_get(
    salts_crypto_sha256_ctx_t *context) {
  return (salts_crypto_sha256_impl *)(void *)context->bytes;
}

int salts_crypto_sha256_init(salts_crypto_sha256_ctx_t *context) {
  salts_crypto_sha256_impl *impl;
  if (context == NULL) return SALTS_CRYPTO_EINVAL;
  memset(context, 0, sizeof(*context));
  impl = salts_crypto_sha256_impl_get(context);
  if (sha256_init(&impl->native) != 0) {
    salts_crypto_secure_zero(context, sizeof(*context));
    return SALTS_CRYPTO_ECRYPTO;
  }
  impl->initialized = 1;
  return SALTS_CRYPTO_OK;
}

int salts_crypto_sha256_update(salts_crypto_sha256_ctx_t *context, const void *data,
                               size_t data_size) {
  salts_crypto_sha256_impl *impl;
  const uint8_t *cursor = (const uint8_t *)data;

  if (context == NULL || (data == NULL && data_size != 0u)) return SALTS_CRYPTO_EINVAL;
  impl = salts_crypto_sha256_impl_get(context);
  if (!impl->initialized) return SALTS_CRYPTO_ESTATE;

  while (data_size != 0u) {
    const u32 chunk = data_size > UINT32_MAX ? UINT32_MAX : (u32)data_size;
    if (sha256_update(&impl->native, cursor, chunk) != 0) return SALTS_CRYPTO_ECRYPTO;
    cursor += chunk;
    data_size -= chunk;
  }
  return SALTS_CRYPTO_OK;
}

int salts_crypto_sha256_final(salts_crypto_sha256_ctx_t *context,
                              uint8_t digest[SALTS_CRYPTO_SHA256_DIGEST_SIZE]) {
  salts_crypto_sha256_impl *impl;
  int result;

  if (context == NULL || digest == NULL) return SALTS_CRYPTO_EINVAL;
  impl = salts_crypto_sha256_impl_get(context);
  if (!impl->initialized) return SALTS_CRYPTO_ESTATE;

  result = sha256_final(&impl->native, digest) == 0 ? SALTS_CRYPTO_OK : SALTS_CRYPTO_ECRYPTO;
  salts_crypto_secure_zero(&impl->native, sizeof(impl->native));
  impl->initialized = 0;
  if (result != SALTS_CRYPTO_OK)
    salts_crypto_secure_zero(digest, SALTS_CRYPTO_SHA256_DIGEST_SIZE);
  return result;
}

int salts_crypto_sha256(const void *data, size_t data_size,
                        uint8_t digest[SALTS_CRYPTO_SHA256_DIGEST_SIZE]) {
  salts_crypto_sha256_ctx_t context;
  int result;

  if (digest == NULL || (data == NULL && data_size != 0u)) return SALTS_CRYPTO_EINVAL;
  result = salts_crypto_sha256_init(&context);
  if (result == SALTS_CRYPTO_OK) result = salts_crypto_sha256_update(&context, data, data_size);
  if (result == SALTS_CRYPTO_OK) result = salts_crypto_sha256_final(&context, digest);
  salts_crypto_secure_zero(&context, sizeof(context));
  if (result != SALTS_CRYPTO_OK)
    salts_crypto_secure_zero(digest, SALTS_CRYPTO_SHA256_DIGEST_SIZE);
  return result;
}

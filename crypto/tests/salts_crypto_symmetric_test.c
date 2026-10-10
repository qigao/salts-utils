#include <salts/crypto.h>
#include <tinytest.h>
#include "crypto_test_hex.h"
#include "crypto_key_vectors.h"

static const char message[] = "Salts key interoperability";

spec("Salts symmetric crypto") {
  it("matches independent digest, HMAC and long-salt PBKDF2 fixtures") {
    uint8_t result[64], expected[64], salt[80];
    size_t i, n, size;
    for (i = 0; i < sizeof(salt); ++i) salt[i] = (uint8_t)i;
    for (i = 0; i < 4; ++i) {
      salts_crypto_hash hash = (salts_crypto_hash)symmetric_vectors[i].hash;
      n = unhex(symmetric_vectors[i].digest, expected);
      check_equal(salts_crypto_digest(hash, message, sizeof(message)-1, result, sizeof(result), &size), SALTS_CRYPTO_OK);
      check_equal(size, n); check_equal(result, expected, n);
      n = unhex(symmetric_vectors[i].mac, expected);
      check_equal(salts_crypto_hmac(hash, "key", 3, message, sizeof(message)-1, result, sizeof(result), &size), SALTS_CRYPTO_OK);
      check_equal(size, n); check_equal(result, expected, n);
      n = unhex(symmetric_vectors[i].pbkdf2, expected);
      check_equal(salts_crypto_pbkdf2(hash, "password", 8, salt, sizeof(salt), 2, result, sizeof(result)), SALTS_CRYPTO_OK);
      check_equal(result, expected, n);
    }
    check_equal(salts_crypto_pbkdf2(SALTS_CRYPTO_SHA256, "", 0, salt, 1, 0, result, 32), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_digest(SALTS_CRYPTO_SHA512, message, sizeof(message)-1, result, 63, &size), SALTS_CRYPTO_EINVAL);
    check_equal(size, (size_t)0);
  }

  it("matches AES-GCM vectors and releases plaintext only after authentication") {
    uint8_t key[32], iv[12], output[64], expected[64], tag[16];
    size_t i, v, size = sizeof(message)-1;
    for (i = 0; i < sizeof(key); ++i) key[i] = (uint8_t)i;
    for (i = 0; i < sizeof(iv); ++i) iv[i] = (uint8_t)i;
    for (v = 0; v < 3; ++v) {
      unhex(gcm_vectors[v].ciphertext, expected);
      check_equal(salts_crypto_aes_gcm_encrypt(key, gcm_vectors[v].key_size, iv, sizeof(iv), "aad", 3,
        (const uint8_t *)message, size, output, sizeof(output), tag, sizeof(tag)), SALTS_CRYPTO_OK);
      check_equal(output, expected, size); check_equal(tag, expected+size, 16);
      tag[0] ^= 1;
      check_equal(salts_crypto_aes_gcm_decrypt(key, gcm_vectors[v].key_size, iv, sizeof(iv), "aad", 3,
        output, size, tag, 16, output, sizeof(output)), SALTS_CRYPTO_EVERIFY);
      check_equal(output, expected, size);
      tag[0] ^= 1;
      check_equal(salts_crypto_aes_gcm_decrypt(key, gcm_vectors[v].key_size, iv, sizeof(iv), "aad", 3,
        output, size, tag, 16, output, sizeof(output)), SALTS_CRYPTO_OK);
      check_equal(output, message, size);
      check_equal(salts_crypto_aes_gcm_encrypt(key, gcm_vectors[v].key_size, iv, sizeof(iv), NULL, 0,
        NULL, 0, NULL, 0, tag, 16), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_aes_gcm_decrypt(key, gcm_vectors[v].key_size, iv, sizeof(iv), NULL, 0,
        NULL, 0, tag, 16, NULL, 0), SALTS_CRYPTO_OK);
    }
    check_equal(salts_crypto_aes_gcm_encrypt(key, 15, iv, 12, NULL, 0, NULL, 0, output, 64, tag, 16), SALTS_CRYPTO_EINVAL);
  }
}

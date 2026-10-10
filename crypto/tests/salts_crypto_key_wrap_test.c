#include <salts/crypto.h>
#include <tinytest.h>
#include <string.h>

#include "crypto_test_hex.h"

/* RFC 3394 section 4, all six independent known-answer vectors. */
static const struct {
  size_t key_size;
  const char *plain;
  const char *wrapped;
} vectors[] = {
  {16, "00112233445566778899aabbccddeeff",
   "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5"},
  {24, "00112233445566778899aabbccddeeff",
   "96778b25ae6ca435f92b5b97c050aed2468ab8a17ad84e5d"},
  {32, "00112233445566778899aabbccddeeff",
   "64e8c3f9ce0f5ba263e9777905818a2a93c8191e7d6e8ae7"},
  {24, "00112233445566778899aabbccddeeff0001020304050607",
   "031d33264e15d33268f24ec260743edce1c6c7ddee725a936ba814915c6762d2"},
  {32, "00112233445566778899aabbccddeeff0001020304050607",
   "a8f9bc1612c68b3ff6e6f4fbe30e71e4769c8b80a32cb8958cd5d17d6b254da1"},
  {32, "00112233445566778899aabbccddeeff000102030405060708090a0b0c0d0e0f",
   "28c9f404c4b810f4cbccb35cfb87f8263f5786e2d80ed326cbc7f0e71a99f43bfb988b9b7a02dd21"}
};

spec("Salts crypto RFC 3394") {
  it("matches every published wrap and unwrap vector, including overlap") {
    uint8_t key[32], plain[32], expected[40], output[48];
    size_t v, i, n, m, size;
    for (i = 0; i < sizeof(key); ++i) key[i] = (uint8_t)i;
    for (v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      n = unhex(vectors[v].plain, plain);
      m = unhex(vectors[v].wrapped, expected);
      check_equal(salts_crypto_aes_key_wrap(key, vectors[v].key_size, plain, n,
                    output, sizeof(output), &size), SALTS_CRYPTO_OK);
      check_equal(size, m);
      check_equal(output, expected, m);
      check_equal(salts_crypto_aes_key_unwrap(key, vectors[v].key_size, expected, m,
                    output, sizeof(output), &size), SALTS_CRYPTO_OK);
      check_equal(size, n);
      check_equal(output, plain, n);
      check_equal(salts_crypto_aes_key_wrap(key, vectors[v].key_size, output, n,
                    output, sizeof(output), &size), SALTS_CRYPTO_OK);
      check_equal(output, expected, m);
      check_equal(salts_crypto_aes_key_unwrap(key, vectors[v].key_size, output, m,
                    output, sizeof(output), &size), SALTS_CRYPTO_OK);
      check_equal(output, plain, n);
      memcpy(output, plain, n);
      check_equal(salts_crypto_aes_key_wrap(key, vectors[v].key_size, output, n,
                    output + 1, sizeof(output) - 1, &size), SALTS_CRYPTO_OK);
      check_equal(output + 1, expected, m);
      check_equal(salts_crypto_aes_key_unwrap(key, vectors[v].key_size, output + 1, m,
                    output, sizeof(output), &size), SALTS_CRYPTO_OK);
      check_equal(output, plain, n);
    }
  }

  it("rejects tampering and wrong KEKs, erasing all candidate plaintext") {
    uint8_t key[16], ciphertext[24], output[24], zero[16] = {0};
    size_t i, size;
    for (i = 0; i < sizeof(key); ++i) key[i] = (uint8_t)i;
    unhex(vectors[0].wrapped, ciphertext);
    for (i = 0; i <= sizeof(ciphertext); ++i) {
      if (i < sizeof(ciphertext)) ciphertext[i] ^= 1;
      else key[0] ^= 1;
      memset(output, 0x5a, sizeof(output));
      size = 123;
      check_equal(salts_crypto_aes_key_unwrap(key, sizeof(key), ciphertext,
                    sizeof(ciphertext), output, sizeof(output), &size), SALTS_CRYPTO_EVERIFY);
      check_equal(size, 0U);
      check_equal(output, zero, sizeof(zero));
      check_equal(output[16], 0x5a);
      if (i < sizeof(ciphertext)) ciphertext[i] ^= 1;
    }
  }

  it("rejects invalid sizes and short outputs before writing") {
    uint8_t key[16] = {0}, input[24] = {0}, output[32], expected[32];
    size_t size = 1;
    memset(output, 0x5a, sizeof(output));
    memcpy(expected, output, sizeof(output));
    check_equal(salts_crypto_aes_key_wrap(key, 15, input, 16, output, 24, &size), SALTS_CRYPTO_EINVAL);
    check_equal(size, 0U);
    check_equal(salts_crypto_aes_key_wrap(key, 16, input, 16, output, 23, &size), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_aes_key_wrap(key, 16, input, 8, output, 32, &size), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_aes_key_wrap(key, 16, input, 17, output, 32, &size), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_aes_key_wrap(key, 16, input, SIZE_MAX - 7, output, SIZE_MAX, &size), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_aes_key_unwrap(key, 16, input, 16, output, 32, &size), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_aes_key_unwrap(key, 16, input, 24, output, 15, &size), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_aes_key_unwrap(NULL, 16, input, 24, output, 32, &size), SALTS_CRYPTO_EINVAL);
    check_equal(output, expected, sizeof(output));
  }
}

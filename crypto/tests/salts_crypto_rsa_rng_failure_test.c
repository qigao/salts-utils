#include <gmssl/rsa_ext.h>
#include <gmssl/rand.h>
#include <gmssl/digest.h>
#include <gmssl/asn1.h>
#include <tinytest.h>
#include "crypto_test_hex.h"
#include "crypto_rsa_vectors.h"

/* Test executable only. Resolve the static provider's RNG dependency here so
 * each failure position is deterministic; no production injection hook. */
static size_t random_calls, fail_call;
static int zeros_only;
int rand_bytes(uint8_t *out, size_t size) {
  ++random_calls;
  if (random_calls == fail_call) return -1;
  memset(out, 0, size);
  if (size && !zeros_only) out[size - 1] = 2;
  return 1;
}

spec("GmSSL RSA random failure") {
  it("fails closed on n/e/d blinding failure and verifies private results") {
    RSA_COMPONENTS key;
    uint8_t der[4096], input[256] = {0}, output[256], saved[256];
    const uint8_t *cursor = der, *seq, *n, *e, *d;
    size_t len = unhex(rsa_vectors[0].der, der), seq_len, nlen, elen, dlen, size;
    int version;
    check_equal(asn1_sequence_from_der(&seq, &seq_len, &cursor, &len), 1);
    check_equal(asn1_int_from_der(&version, &seq, &seq_len), 1);
    check_equal(asn1_integer_from_der(&n, &nlen, &seq, &seq_len), 1);
    check_equal(asn1_integer_from_der(&e, &elen, &seq, &seq_len), 1);
    check_equal(asn1_integer_from_der(&d, &dlen, &seq, &seq_len), 1);
    check_equal(rsa_components_import(&key, n, nlen, e, elen, d, dlen), 1);
    input[255] = 42;
    memset(saved, 0xa5, sizeof(saved)); memcpy(output, saved, sizeof(output));
    random_calls = 0; fail_call = 1; zeros_only = 0;
    check_equal(rsa_components_private(&key, input, sizeof(input), output, sizeof(output), &size), -1);
    check_equal(size, (size_t)0); check_equal(output, saved, sizeof(output));
    random_calls = fail_call = 0; zeros_only = 1;
    check_equal(rsa_components_private(&key, input, sizeof(input), output, sizeof(output), &size), -1);
    check_equal(random_calls, (size_t)8); check_equal(size, (size_t)0);
    check_equal(output, saved, sizeof(output));
    random_calls = fail_call = 0; zeros_only = 0;
    key.d[key.size-1] ^= 1;
    check_equal(rsa_components_private(&key, input, sizeof(input), output, sizeof(output), &size), -1);
    check_equal(size, (size_t)0); check_equal(output, saved, sizeof(output));
    rsa_components_cleanup(&key);
  }

  it("matches independently verified PSS and OAEP encodings with controlled test randomness") {
    static const uint8_t message[] = "Salts RSA provider contract";
    const DIGEST *hashes[] = {DIGEST_sha256(), DIGEST_sha384(), DIGEST_sha512()};
    RSA_PRIVATE_KEY key;
    uint8_t der[4096], dig[64], cek[32];
    uint8_t out[RSA_MAX_MODULUS_SIZE], expected[RSA_MAX_MODULUS_SIZE];
    size_t v, a, n, size, diglen, expected_len;
    const uint8_t *p;
    fail_call = random_calls = 0;
    zeros_only = 0;
    for (n = 0; n < sizeof(cek); ++n) cek[n] = (uint8_t)n;
    for (v = 0; v < RSA_VECTOR_COUNT; ++v) {
      n = unhex(rsa_vectors[v].der, der);
      p = der;
      check_equal(rsa_private_key_from_der(&key, &p, &n), 1);
      for (a = 0; a < 3; ++a) {
        check_equal(digest(hashes[a], message, sizeof(message) - 1, dig, &diglen), 1);
        expected_len = unhex(rsa_vectors[v].pss_salt_two[a], expected);
        check_equal(rsa_sign_pss_digest(&key, (RSA_HASH)(RSA_HASH_SHA256 + a), dig, diglen,
                      diglen, out, sizeof(out), &size), 1);
        check_equal(size, expected_len);
        check_equal(out, expected, size);
      }
      for (a = 0; a < 2; ++a) {
        expected_len = unhex(rsa_vectors[v].oaep_seed_two[a], expected);
        check_equal(rsa_oaep_encrypt(&key.public_key, a ? RSA_HASH_SHA256 : RSA_HASH_SHA1,
                      NULL, 0, cek, sizeof(cek), out, sizeof(out), &size), 1);
        check_equal(size, expected_len);
        check_equal(out, expected, size);
      }
      rsa_private_key_cleanup(&key);
    }
  }

  it("never falls back to unblinded CRT and bounds zero-factor retries") {
    RSA_PRIVATE_KEY key;
    uint8_t der[4096], in[RSA_MAX_MODULUS_SIZE] = {0};
    uint8_t out[RSA_MAX_MODULUS_SIZE], expected[RSA_MAX_MODULUS_SIZE];
    size_t n = unhex(rsa_vectors[0].der, der), size, failure;
    const uint8_t *p = der;
    check_equal(rsa_private_key_from_der(&key, &p, &n), 1);
    check_equal(n, 0U);
    in[key.public_key.modulus_size - 1] = 42;
    memset(out, 0x5a, sizeof(out));
    memcpy(expected, out, sizeof(out));
    zeros_only = 0;
    for (failure = 1; failure <= 4; ++failure) {
      random_calls = 0;
      fail_call = failure;
      size = 123;
      check_equal(rsa_private_key_operation(&key, in, key.public_key.modulus_size,
                    out, sizeof(out), &size), -1);
      check_equal(size, 0U);
      check_equal(out, expected, sizeof(out));
      check_equal(random_calls, failure);
    }
    random_calls = fail_call = 0;
    zeros_only = 1;
    check_equal(rsa_private_key_operation(&key, in, key.public_key.modulus_size,
                  out, sizeof(out), &size), -1);
    check_equal(size, 0U);
    check_equal(out, expected, sizeof(out));
    check_equal(random_calls, 8U);
    random_calls = 0;
    zeros_only = 0;
    check_equal(rsa_private_key_operation(&key, in, key.public_key.modulus_size,
                  out, sizeof(out), &size), 1);
    check_equal(random_calls, 4U);
    n = unhex(rsa_vectors[0].raw, expected);
    check_equal(size, n);
    check_equal(out, expected, n);
    rsa_private_key_cleanup(&key);
  }

  it("fails closed when PSS salt or OAEP seed generation fails") {
    RSA_PRIVATE_KEY key;
    uint8_t der[4096], dig[32] = {0}, out[RSA_MAX_MODULUS_SIZE], saved[RSA_MAX_MODULUS_SIZE];
    size_t n = unhex(rsa_vectors[0].der, der), size;
    const uint8_t *p = der;
    check_equal(rsa_private_key_from_der(&key, &p, &n), 1);
    memset(out, 0x5a, sizeof(out));
    memcpy(saved, out, sizeof(out));
    random_calls = 0;
    fail_call = 1;
    zeros_only = 0;
    check_equal(rsa_sign_pss_digest(&key, RSA_HASH_SHA256, dig, sizeof(dig), 32,
                  out, sizeof(out), &size), -1);
    check_equal(random_calls, 1U);
    check_equal(size, 0U);
    check_equal(out, saved, sizeof(out));
    random_calls = 0;
    check_equal(rsa_oaep_encrypt(&key.public_key, RSA_HASH_SHA256, NULL, 0, dig, sizeof(dig),
                  out, sizeof(out), &size), -1);
    check_equal(random_calls, 1U);
    check_equal(size, 0U);
    check_equal(out, saved, sizeof(out));
    rsa_private_key_cleanup(&key);
  }
}

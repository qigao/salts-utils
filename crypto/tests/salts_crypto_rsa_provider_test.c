#include <gmssl/rsa.h>
#include <gmssl/rsa_ext.h>
#include <gmssl/digest.h>
#include <gmssl/rsa_components.h>
#include <gmssl/asn1.h>
#include <tinytest.h>
#include "crypto_test_hex.h"
#include "crypto_rsa_vectors.h"

static void load_key(size_t index, RSA_PRIVATE_KEY *key) {
  uint8_t der[4096];
  size_t n = unhex(rsa_vectors[index].der, der);
  const uint8_t *p = der;
  check_equal(rsa_private_key_from_der(key, &p, &n), 1);
  check_equal(n, 0U);
}

spec("GmSSL RSA provider") {
  it("uses blinded n/e/d operations without CRT factors") {
    RSA_COMPONENTS key;
    uint8_t der[4096], input[RSA_COMPONENTS_MAX_SIZE] = {0};
    uint8_t output[RSA_COMPONENTS_MAX_SIZE], expected[RSA_COMPONENTS_MAX_SIZE];
    size_t v, len, seq_len, nlen, elen, dlen, size, expected_len;
    const uint8_t *cursor, *seq, *n, *e, *d;
    int version;
    for (v = 0; v < RSA_VECTOR_COUNT; ++v) {
      RSA_PRIVATE_KEY crt;
      len = unhex(rsa_vectors[v].der, der); cursor = der;
      check_equal(asn1_sequence_from_der(&seq, &seq_len, &cursor, &len), 1);
      check_equal(asn1_int_from_der(&version, &seq, &seq_len), 1);
      check_equal(asn1_integer_from_der(&n, &nlen, &seq, &seq_len), 1);
      check_equal(asn1_integer_from_der(&e, &elen, &seq, &seq_len), 1);
      check_equal(asn1_integer_from_der(&d, &dlen, &seq, &seq_len), 1);
      while (nlen > 1 && !*n) { ++n; --nlen; }
      while (elen > 1 && !*e) { ++e; --elen; }
      while (dlen > 1 && !*d) { ++d; --dlen; }
      check_equal(rsa_components_import(&key, n, nlen, e, elen, d, dlen), 1);
      load_key(v, &crt);
      check_equal(rsa_components_check_crt(&key, crt.prime1, crt.prime_size,
        crt.prime2, crt.prime_size, crt.exponent1, crt.prime_size,
        crt.exponent2, crt.prime_size, crt.coefficient, crt.prime_size), 1);
      crt.coefficient[crt.prime_size-1] ^= 1;
      check_equal(rsa_components_check_crt(&key, crt.prime1, crt.prime_size,
        crt.prime2, crt.prime_size, crt.exponent1, crt.prime_size,
        crt.exponent2, crt.prime_size, crt.coefficient, crt.prime_size), -1);
      rsa_private_key_cleanup(&crt);
      memset(input, 0, sizeof(input)); input[nlen - 1] = 42;
      expected_len = unhex(rsa_vectors[v].raw, expected);
      check_equal(rsa_components_private(&key, input, nlen, output, sizeof(output), &size), 1);
      check_equal(size, expected_len);
      check_equal(output, expected, size);
      check_equal(rsa_components_public(&key, output, size, output, sizeof(output), &size), 1);
      check_equal(output, input, nlen);
      rsa_components_cleanup(&key);
    }
  }

  it("matches independent raw RSA answers including a non-byte-aligned modulus") {
    RSA_PRIVATE_KEY key;
    uint8_t in[RSA_MAX_MODULUS_SIZE] = {0}, out[RSA_MAX_MODULUS_SIZE];
    uint8_t expected[RSA_MAX_MODULUS_SIZE];
    size_t v, size, repeat;
    for (v = 0; v < RSA_VECTOR_COUNT; ++v) {
      load_key(v, &key);
      memset(in, 0, sizeof(in));
      in[key.public_key.modulus_size - 1] = 42;
      size = unhex(rsa_vectors[v].raw, expected);
      check_equal(size, key.public_key.modulus_size);
      for (repeat = 0; repeat < 2; ++repeat) {
        check_equal(rsa_private_key_operation(&key, in, size, out, sizeof(out), &size), 1);
        check_equal(size, key.public_key.modulus_size);
        check_equal(out, expected, size);
      }
      rsa_private_key_cleanup(&key);
    }
  }

  it("rejects out-of-range representatives and invalid CRT results without output") {
    RSA_PRIVATE_KEY key;
    uint8_t in[RSA_MAX_MODULUS_SIZE] = {0}, out[RSA_MAX_MODULUS_SIZE];
    uint8_t untouched[RSA_MAX_MODULUS_SIZE];
    size_t size = 123;
    load_key(0, &key);
    memset(out, 0x5a, sizeof(out));
    memcpy(untouched, out, sizeof(out));
    check_equal(rsa_private_key_operation(&key, key.public_key.modulus,
                  key.public_key.modulus_size, out, sizeof(out), &size), -1);
    check_equal(size, 0U);
    check_equal(out, untouched, sizeof(out));
    in[key.public_key.modulus_size - 1] = 42;
    key.coefficient[key.prime_size - 1] ^= 1;
    check_equal(rsa_private_key_operation(&key, in, key.public_key.modulus_size,
                  out, sizeof(out), &size), -1);
    check_equal(size, 0U);
    check_equal(out, untouched, sizeof(out));
    rsa_private_key_cleanup(&key);
  }

  it("handles zero, one and overlapping buffers and rejects insufficient capacity") {
    RSA_PRIVATE_KEY key;
    uint8_t in[RSA_MAX_MODULUS_SIZE] = {0}, saved[RSA_MAX_MODULUS_SIZE];
    size_t size, v;
    load_key(0, &key);
    for (v = 0; v < 2; ++v) {
      in[key.public_key.modulus_size - 1] = (uint8_t)v;
      memcpy(saved, in, sizeof(in));
      check_equal(rsa_private_key_operation(&key, in, key.public_key.modulus_size,
                    in, sizeof(in), &size), 1);
      check_equal(size, key.public_key.modulus_size);
      check_equal(in, saved, sizeof(in));
    }
    check_equal(rsa_private_key_operation(&key, in, key.public_key.modulus_size,
                  in, key.public_key.modulus_size - 1, &size), -1);
    check_equal(size, 0U);
    check_equal(in, saved, sizeof(in));
    rsa_private_key_cleanup(&key);
  }

  it("matches independent PKCS1 and PSS SHA256/384/512 signatures at every key size") {
    static const uint8_t message[] = "Salts RSA provider contract";
    const DIGEST *hashes[] = {DIGEST_sha256(), DIGEST_sha384(), DIGEST_sha512()};
    RSA_PRIVATE_KEY key;
    uint8_t dig[64], sig[RSA_MAX_MODULUS_SIZE], actual[RSA_MAX_MODULUS_SIZE];
    size_t v, a, scheme, diglen, siglen, actual_len;
    for (v = 0; v < RSA_VECTOR_COUNT; ++v) {
      load_key(v, &key);
      for (a = 0; a < 3; ++a) {
        RSA_HASH hash = (RSA_HASH)(RSA_HASH_SHA256 + a);
        check_equal(digest(hashes[a], message, sizeof(message) - 1, dig, &diglen), 1);
        for (scheme = 0; scheme < 2; ++scheme) {
          siglen = unhex(rsa_vectors[v].signatures[3 * scheme + a], sig);
          if (scheme == 0) {
            check_equal(rsa_verify_pkcs1_v15_digest(&key.public_key, hash, dig, diglen, sig, siglen), 1);
            check_equal(rsa_sign_pkcs1_v15_digest(&key, hash, dig, diglen,
                          actual, sizeof(actual), &actual_len), 1);
            check_equal(actual_len, siglen);
            check_equal(actual, sig, siglen);
            if (a == 0) {
              check_equal(rsa_verify_pkcs1_v15_sha256(&key.public_key, dig, sig, siglen), 1);
              check_equal(rsa_sign_pkcs1_v15_sha256(&key, dig, actual, sizeof(actual), &actual_len), 1);
              check_equal(actual_len, siglen);
              check_equal(actual, sig, siglen);
            }
            sig[0] ^= 1;
            check_equal(rsa_verify_pkcs1_v15_digest(&key.public_key, hash, dig, diglen, sig, siglen), 0);
          } else {
            check_equal(rsa_verify_pss_digest(&key.public_key, hash, dig, diglen, diglen, sig, siglen), 1);
            if (a == 0) {
              check_equal(rsa_verify_pss_sha256(&key.public_key, dig, sig, siglen), 1);
              check_equal(rsa_sign_pss_sha256(&key, dig, actual, sizeof(actual), &actual_len), 1);
              check_equal(rsa_verify_pss_digest(&key.public_key, hash, dig, diglen,
                            diglen, actual, actual_len), 1);
            }
            check_equal(rsa_verify_pss_digest(&key.public_key, hash, dig, diglen, RSA_PSS_SALT_AUTO, sig, siglen), 1);
            check_equal(rsa_verify_pss_digest(&key.public_key, hash, dig, diglen, 0, sig, siglen), 0);
            check_equal(rsa_sign_pss_digest(&key, hash, dig, diglen, diglen,
                          actual, sizeof(actual), &actual_len), 1);
            check_equal(rsa_verify_pss_digest(&key.public_key, hash, dig, diglen,
                          diglen, actual, actual_len), 1);
            sig[siglen - 1] ^= 1;
            check_equal(rsa_verify_pss_digest(&key.public_key, hash, dig, diglen,
                          RSA_PSS_SALT_AUTO, sig, siglen), 0);
          }
        }
      }
      rsa_private_key_cleanup(&key);
    }
  }

  it("supports explicit zero and maximum PSS salt lengths without weakening strict verification") {
    RSA_PRIVATE_KEY key;
    uint8_t dig[32] = {0}, sig[RSA_MAX_MODULUS_SIZE], saved[RSA_MAX_MODULUS_SIZE];
    size_t size, salts[2], i;
    load_key(2, &key);
    salts[0] = 0;
    salts[1] = key.public_key.modulus_size - sizeof(dig) - 2;
    for (i = 0; i < 2; ++i) {
      check_equal(rsa_sign_pss_digest(&key, RSA_HASH_SHA256, dig, sizeof(dig), salts[i],
                    sig, sizeof(sig), &size), 1);
      check_equal(rsa_verify_pss_digest(&key.public_key, RSA_HASH_SHA256, dig, sizeof(dig),
                    salts[i], sig, size), 1);
      check_equal(rsa_verify_pss_digest(&key.public_key, RSA_HASH_SHA256, dig, sizeof(dig),
                    RSA_PSS_SALT_AUTO, sig, size), 1);
      check_equal(rsa_verify_pss_digest(&key.public_key, RSA_HASH_SHA256, dig, sizeof(dig),
                    32, sig, size), 0);
    }
    memcpy(saved, sig, sizeof(sig));
    check_equal(rsa_sign_pss_digest(&key, RSA_HASH_SHA256, dig, sizeof(dig), SIZE_MAX,
                  sig, sizeof(sig), &size), -1);
    check_equal(size, 0U);
    check_equal(sig, saved, sizeof(sig));
    check_equal(rsa_sign_pkcs1_v15_digest(&key, RSA_HASH_SHA1, dig, 20,
                  sig, sizeof(sig), &size), -1);
    check_equal(size, 0U);
    check_equal(sig, saved, sizeof(sig));
    rsa_private_key_cleanup(&key);
  }

  it("decrypts independent OAEP SHA1/SHA256 ciphertexts and fails without partial output") {
    RSA_PRIVATE_KEY key;
    uint8_t ct[RSA_MAX_MODULUS_SIZE], out[RSA_MAX_MODULUS_SIZE];
    uint8_t expected[32], saved[RSA_MAX_MODULUS_SIZE];
    size_t v, a, i, ctlen, size;
    for (i = 0; i < sizeof(expected); ++i) expected[i] = (uint8_t)i;
    for (v = 0; v < RSA_VECTOR_COUNT; ++v) {
      load_key(v, &key);
      for (a = 0; a < 2; ++a) {
        RSA_HASH hash = a ? RSA_HASH_SHA256 : RSA_HASH_SHA1;
        ctlen = unhex(rsa_vectors[v].oaep[a], ct);
        check_equal(rsa_oaep_decrypt(&key, hash, NULL, 0, ct, ctlen, out, sizeof(out), &size), 1);
        check_equal(size, sizeof(expected));
        check_equal(out, expected, sizeof(expected));
        memset(out, 0x5a, sizeof(out));
        memcpy(saved, out, sizeof(out));
        check_equal(rsa_oaep_decrypt(&key, hash, (const uint8_t *)"wrong", 5,
                      ct, ctlen, out, sizeof(out), &size), 0);
        check_equal(size, 0U);
        check_equal(out, saved, sizeof(out));
        check_equal(rsa_oaep_decrypt(&key, hash, NULL, 0, ct, ctlen, out, 31, &size), 0);
        check_equal(size, 0U);
        check_equal(out, saved, sizeof(out));
        check_equal(rsa_oaep_decrypt(&key, hash, NULL, 0, ct, ctlen - 1, out, sizeof(out), &size), 0);
        check_equal(size, 0U);
        check_equal(out, saved, sizeof(out));
        ct[ctlen - 1] ^= 1;
        check_equal(rsa_oaep_decrypt(&key, hash, NULL, 0, ct, ctlen, out, sizeof(out), &size), 0);
        check_equal(size, 0U);
        check_equal(out, saved, sizeof(out));
      }
      rsa_private_key_cleanup(&key);
    }
  }

  it("round trips empty and maximum OAEP payloads with labels and in-place buffers") {
    static const uint8_t label[] = {0, 1, 0xff, 0};
    static const size_t hash_sizes[] = {20, 32, 48, 64};
    RSA_PRIVATE_KEY key;
    uint8_t buffer[RSA_MAX_MODULUS_SIZE], expected[RSA_MAX_MODULUS_SIZE];
    size_t a, i, n, size, ctlen, hlen;
    load_key(0, &key);
    for (a = 0; a < 4; ++a) {
      RSA_HASH hash = (RSA_HASH)(RSA_HASH_SHA1 + a);
      hlen = hash_sizes[a];
      for (n = 0; n < 2; ++n) {
        size_t len = n ? key.public_key.modulus_size - 2 * hlen - 2 : 0;
        for (i = 0; i < len; ++i) expected[i] = buffer[i] = (uint8_t)i;
        check_equal(rsa_oaep_encrypt(&key.public_key, hash, label, sizeof(label),
                      buffer, len, buffer, sizeof(buffer), &ctlen), 1);
        check_equal(rsa_oaep_decrypt(&key, hash, label, sizeof(label),
                      buffer, ctlen, buffer, sizeof(buffer), &size), 1);
        check_equal(size, len);
        check_equal(buffer, expected, len);
      }
      memcpy(expected, buffer, sizeof(buffer));
      check_equal(rsa_oaep_encrypt(&key.public_key, hash, NULL, 0, buffer,
                    key.public_key.modulus_size - 2 * hlen - 1, buffer, sizeof(buffer), &size), -1);
      check_equal(size, 0U);
      check_equal(buffer, expected, sizeof(buffer));
    }
    rsa_private_key_cleanup(&key);
  }

  it("returns the same failure for every invalid OAEP encoding without releasing plaintext") {
    RSA_PRIVATE_KEY key;
    uint8_t ct[RSA_MAX_MODULUS_SIZE], out[RSA_MAX_MODULUS_SIZE], saved[RSA_MAX_MODULUS_SIZE];
    size_t v, variant, ctlen, size;
    memset(out, 0x5a, sizeof(out));
    memcpy(saved, out, sizeof(out));
    for (v = 0; v < RSA_VECTOR_COUNT; ++v) {
      load_key(v, &key);
      for (variant = 0; variant < 5; ++variant) {
        ctlen = unhex(rsa_vectors[v].oaep_invalid[variant], ct);
        check_equal(rsa_oaep_decrypt(&key, RSA_HASH_SHA256, NULL, 0,
                      ct, ctlen, out, sizeof(out), &size), 0);
        check_equal(size, 0U);
        check_equal(out, saved, sizeof(out));
      }
      rsa_private_key_cleanup(&key);
    }
  }
}

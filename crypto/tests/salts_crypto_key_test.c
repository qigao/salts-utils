#include <salts/crypto.h>
#include <tinytest.h>
#include "crypto_test_hex.h"
#include "crypto_key_vectors.h"
#include "crypto_explicit_ec_vectors.h"

static const char message[] = "Salts key interoperability";

spec("Salts crypto imported keys") {
  it("imports independent PEM fixtures and verifies all JOSE key families") {
    size_t i;
    for (i = 0; i < sizeof(key_vectors)/sizeof(key_vectors[0]); ++i) {
      salts_crypto_key *private_key = NULL, *public_key = NULL, *traditional = NULL;
      uint8_t signature[1024], expected[1024];
      size_t size = 0, expected_size = unhex(key_vectors[i].signature, expected);
      salts_crypto_signature scheme = key_vectors[i].kind == 1 ? SALTS_CRYPTO_RSA_PKCS1 :
        key_vectors[i].kind == 2 ? SALTS_CRYPTO_ECDSA : SALTS_CRYPTO_EDDSA;
      salts_crypto_hash hash = (salts_crypto_hash)key_vectors[i].hash;
      check_equal(salts_crypto_key_from_pem(key_vectors[i].private_pem,
        strlen(key_vectors[i].private_pem), 1, &private_key), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_from_pem(key_vectors[i].public_pem,
        strlen(key_vectors[i].public_pem), 0, &public_key), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_type(private_key), key_vectors[i].kind);
      check_equal(salts_crypto_key_signature_size(public_key), expected_size);
      check_equal(salts_crypto_key_verify(public_key, scheme, hash, message, sizeof(message)-1,
        expected, expected_size), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_sign(private_key, scheme, hash, message, sizeof(message)-1,
        signature, sizeof(signature), &size), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_verify(public_key, scheme, hash, message, sizeof(message)-1,
        signature, size), SALTS_CRYPTO_OK);
      if (scheme != SALTS_CRYPTO_ECDSA) check_equal(signature, expected, expected_size);
      signature[0] ^= 1;
      check_not_equal(salts_crypto_key_verify(public_key, scheme, hash, message, sizeof(message)-1,
        signature, size), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_sign(public_key, scheme, hash, message, sizeof(message)-1,
        signature, sizeof(signature), &size), SALTS_CRYPTO_EINVAL);
      check_equal(size, (size_t)0);
      if (*key_vectors[i].traditional_pem) {
        check_equal(salts_crypto_key_from_pem(key_vectors[i].traditional_pem,
          strlen(key_vectors[i].traditional_pem), 1, &traditional), SALTS_CRYPTO_OK);
        check_equal(salts_crypto_key_verify(traditional, scheme, hash, message, sizeof(message)-1,
          expected, expected_size), SALTS_CRYPTO_OK);
      }
      salts_crypto_key_destroy(traditional);
      salts_crypto_key_destroy(public_key);
      salts_crypto_key_destroy(private_key);
    }
  }

  it("supports n/e/d without CRT at 1024 and 8192 bits including a 33-bit exponent") {
    size_t i;
    for (i = 0; i < 2; ++i) {
      uint8_t n[1024], e[8], d[1024], signature[1024], expected[1024], ciphertext[1024], plain[64];
      salts_crypto_bytes bn = {n, unhex(key_vectors[i].n, n)}, be = {e, unhex(key_vectors[i].e, e)},
        bd = {d, unhex(key_vectors[i].d, d)}, empty = {0};
      salts_crypto_key *key = NULL;
      size_t size = 0, expected_size = unhex(key_vectors[i].signature, expected);
      check_equal(salts_crypto_key_from_rsa(bn, be, bd, empty, empty, empty, empty, empty, &key), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_sign(key, SALTS_CRYPTO_RSA_PKCS1, SALTS_CRYPTO_SHA256,
        message, sizeof(message)-1, signature, sizeof(signature), &size), SALTS_CRYPTO_OK);
      check_equal(size, expected_size);
      check_equal(signature, expected, expected_size);
      size = unhex(key_vectors[i].oaep, ciphertext);
      check_equal(salts_crypto_key_oaep_decrypt(key, SALTS_CRYPTO_SHA256, ciphertext, size,
        plain, sizeof(plain), &size), SALTS_CRYPTO_OK);
      check_equal(size, (size_t)16);
      check_equal(plain, "0123456789abcdef", 16);
      check_equal(salts_crypto_key_oaep_encrypt(key, SALTS_CRYPTO_SHA256,
        (const uint8_t *)"0123456789abcdef", 16, ciphertext, sizeof(ciphertext), &size), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_oaep_decrypt(key, SALTS_CRYPTO_SHA256, ciphertext, size,
        plain, sizeof(plain), &size), SALTS_CRYPTO_OK);
      ciphertext[3] ^= 1;
      memset(plain, 0xa5, sizeof(plain));
      check_not_equal(salts_crypto_key_oaep_decrypt(key, SALTS_CRYPTO_SHA256, ciphertext, bn.size,
        plain, sizeof(plain), &size), SALTS_CRYPTO_OK);
      check_equal(size, (size_t)0);
      check_equal(plain[0], 0xa5);
      salts_crypto_key_destroy(key);
    }
  }

  it("accepts compressed SEC1 points on all supported curves") {
    size_t i;
    for (i = 2; i < 6; ++i) {
      uint8_t point[67], signature[132];
      salts_crypto_key *key = NULL;
      salts_crypto_bytes public_key = {point, unhex(key_vectors[i].compressed_pem, point)}, empty = {0};
      size_t size = unhex(key_vectors[i].signature, signature);
      check_equal(salts_crypto_key_from_ec((salts_crypto_ec_curve)key_vectors[i].curve,
        public_key, empty, &key), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_verify(key, SALTS_CRYPTO_ECDSA, (salts_crypto_hash)key_vectors[i].hash,
        message, sizeof(message)-1, signature, size), SALTS_CRYPTO_OK);
      salts_crypto_key_destroy(key);
    }
  }

  it("maps explicit EC parameters to the same compiled named groups") {
    size_t i;
    for (i = 0; i < 4; ++i) {
      salts_crypto_key *priv = NULL, *pub = NULL;
      uint8_t signature[132];
      size_t n = unhex(key_vectors[i+2].signature, signature);
      check_equal(salts_crypto_key_from_pem(explicit_ec_vectors[i].private_pem,
        strlen(explicit_ec_vectors[i].private_pem), 1, &priv), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_from_pem(explicit_ec_vectors[i].public_pem,
        strlen(explicit_ec_vectors[i].public_pem), 0, &pub), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_verify(priv, SALTS_CRYPTO_ECDSA, (salts_crypto_hash)key_vectors[i+2].hash,
        message, sizeof(message)-1, signature, n), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_key_verify(pub, SALTS_CRYPTO_ECDSA, (salts_crypto_hash)key_vectors[i+2].hash,
        message, sizeof(message)-1, signature, n), SALTS_CRYPTO_OK);
      salts_crypto_key_destroy(priv); salts_crypto_key_destroy(pub);
    }
  }

  it("rejects malformed PEM and inconsistent key components") {
    salts_crypto_key *key = NULL;
    salts_crypto_bytes empty = {0}, invalid = {NULL, 1};
    uint8_t seed[32] = {1}, public_key[32] = {0};
    salts_crypto_bytes priv = {seed, sizeof(seed)}, pub = {public_key, sizeof(public_key)};
    check_equal(salts_crypto_key_from_rsa(empty, empty, invalid, empty, empty, empty, empty, empty, &key), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_key_from_ed(SALTS_CRYPTO_KEY_ED25519, pub, priv, &key), SALTS_CRYPTO_EINVAL);
    const char *malformed = "-----BEGIN PUBLIC KEY-----\n!\n-----END PUBLIC KEY-----";
    check_equal(salts_crypto_key_from_pem(malformed, strlen(malformed), 0, &key), SALTS_CRYPTO_EINVAL);
    check(key == NULL);
  }
}

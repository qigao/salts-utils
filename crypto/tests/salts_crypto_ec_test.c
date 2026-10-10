#include <salts/crypto.h>
#include <tinytest.h>
#include "crypto_test_hex.h"
#include "crypto_ecdsa_vectors.h"

spec("Salts crypto elliptic curves") {
  it("matches the RFC 8032 Ed25519 empty-message vector") {
    uint8_t seed[32], public_key[32], expected_key[32], signature[64], expected_sig[64];
    unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed);
    unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", expected_key);
    unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f"
          "b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", expected_sig);
    check_equal(salts_crypto_ed25519_public_key(seed, public_key), SALTS_CRYPTO_OK);
    check_equal(public_key, expected_key, sizeof(public_key));
    check_equal(salts_crypto_ed25519_sign(seed, NULL, 0, signature), SALTS_CRYPTO_OK);
    check_equal(signature, expected_sig, sizeof(signature));
    check_equal(salts_crypto_ed25519_verify(public_key, NULL, 0, signature), SALTS_CRYPTO_OK);
    signature[0] ^= 1;
    check_equal(salts_crypto_ed25519_verify(public_key, NULL, 0, signature), SALTS_CRYPTO_EVERIFY);
    memset(public_key, 0xff, sizeof(public_key));
    check_equal(salts_crypto_ed25519_verify(public_key, NULL, 0, expected_sig), SALTS_CRYPTO_EVERIFY);
  }

  it("generates Ed25519 keys and enforces buffer and message contracts") {
    uint8_t seed[32], public_key[32], signature[64], overlap[33];
    check_equal(salts_crypto_ed25519_keygen(seed, public_key), SALTS_CRYPTO_OK);
    check_equal(salts_crypto_ed25519_sign(seed, "abc", 3, signature), SALTS_CRYPTO_OK);
    check_equal(salts_crypto_ed25519_verify(public_key, "abc", 3, signature), SALTS_CRYPTO_OK);
    check_equal(salts_crypto_ed25519_verify(public_key, "abd", 3, signature), SALTS_CRYPTO_EVERIFY);
    check_equal(salts_crypto_ed25519_keygen(overlap, overlap + 1), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_ed25519_sign(seed, NULL, 1, signature), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_ed25519_sign(seed, "a", (size_t)UINT32_MAX + 1, signature), SALTS_CRYPTO_EINVAL);
  }

  it("derives and verifies the RFC 6979 NIST curve vectors") {
    uint8_t private_key[66], expected[132], public_key[132], signature[132];
    size_t v, n;
    for (v = 0; v < sizeof(ecdsa_vectors) / sizeof(ecdsa_vectors[0]); ++v) {
      n = unhex(ecdsa_vectors[v].private_key, private_key);
      check_equal(unhex(ecdsa_vectors[v].public_key, expected), 2*n);
      check_equal(salts_crypto_ecdsa_public_key(ecdsa_vectors[v].curve,
                    private_key, n, public_key, 2*n), SALTS_CRYPTO_OK);
      check_equal(public_key, expected, 2*n);
      check_equal(unhex(ecdsa_vectors[v].signature, signature), 2*n);
      check_equal(salts_crypto_ecdsa_verify(ecdsa_vectors[v].curve,
                    public_key, 2*n, "sample", 6, signature, 2*n), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_ecdsa_verify(ecdsa_vectors[v].curve,
                    public_key, 2*n, "tamper", 6, signature, 2*n), SALTS_CRYPTO_EVERIFY);
      check_equal(salts_crypto_ecdsa_sign(ecdsa_vectors[v].curve,
                    private_key, n, "sample", 6, signature, 2*n), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_ecdsa_verify(ecdsa_vectors[v].curve,
                    public_key, 2*n, "sample", 6, signature, 2*n), SALTS_CRYPTO_OK);
      memset(signature, 0, 2*n);
      check_equal(salts_crypto_ecdsa_verify(ecdsa_vectors[v].curve,
                    public_key, 2*n, "sample", 6, signature, 2*n), SALTS_CRYPTO_EVERIFY);
      memset(public_key, 0, 2*n);
      check_equal(salts_crypto_ecdsa_verify(ecdsa_vectors[v].curve,
                    public_key, 2*n, "sample", 6, signature, 2*n), SALTS_CRYPTO_EVERIFY);
      memset(private_key, 0, n);
      check_not_equal(salts_crypto_ecdsa_sign(ecdsa_vectors[v].curve,
                        private_key, n, "sample", 6, signature, 2*n), SALTS_CRYPTO_OK);
      check_equal(salts_crypto_ecdsa_public_key(ecdsa_vectors[v].curve,
                    private_key, n, public_key, 2*n), SALTS_CRYPTO_EINVAL);
      memset(private_key, 0xff, n);
      check_not_equal(salts_crypto_ecdsa_sign(ecdsa_vectors[v].curve,
                        private_key, n, "sample", 6, signature, 2*n), SALTS_CRYPTO_OK);
      memset(expected, 0, sizeof(expected));
      check_equal(signature, expected, 2*n);
    }
  }

  it("supports secp256k1 using fixed-width JOSE keys and signatures") {
    uint8_t private_key[32] = {0}, public_key[64], expected[64], signature[64];
    private_key[31] = 1;
    unhex("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
          "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8", expected);
    check_equal(salts_crypto_ecdsa_public_key(SALTS_CRYPTO_EC_SECP256K1,
                  private_key, 32, public_key, 64), SALTS_CRYPTO_OK);
    check_equal(public_key, expected, 64);
    check_equal(salts_crypto_ecdsa_sign(SALTS_CRYPTO_EC_SECP256K1,
                  private_key, 32, NULL, 0, signature, 64), SALTS_CRYPTO_OK);
    check_equal(salts_crypto_ecdsa_verify(SALTS_CRYPTO_EC_SECP256K1,
                  public_key, 64, NULL, 0, signature, 64), SALTS_CRYPTO_OK);
    check_equal(salts_crypto_ecdsa_sign(SALTS_CRYPTO_EC_SECP256K1,
                  private_key, 31, NULL, 0, signature, 64), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_ecdsa_verify(SALTS_CRYPTO_EC_SECP256K1,
                  public_key, 64, NULL, 0, signature, 63), SALTS_CRYPTO_EINVAL);
    check_equal(salts_crypto_ecdsa_verify((salts_crypto_ec_curve)0,
                  public_key, 64, NULL, 0, signature, 64), SALTS_CRYPTO_EINVAL);
  }
}

# Salts Crypto

`Salts::Crypto` provides caller-owned byte-buffer APIs while keeping provider
types private. Its current capabilities are:

- SHA-1/256/384/512 digest, HMAC and PBKDF2, plus streaming SHA-256.
- RFC 8032 pure Ed25519 and Ed448 key generation, public-key derivation, signing
  and verification, using the GmSSL platform CSPRNG.
- ECDSA P-256/SHA-256, P-384/SHA-384, P-521/SHA-512 and secp256k1/SHA-256,
  with fixed-width big-endian scalars, X || Y public keys and R || S signatures.
- RFC 3394 AES Key Wrap/Unwrap with 128-, 192- and 256-bit KEKs.
- AES-GCM with 128/192/256-bit keys, authenticated before plaintext release.
- Immutable owned RSA/EC/EdDSA keys, PEM/component import, PKCS#1/PSS message
  signatures and RSA-OAEP. Provider structures remain private.

```c
#include <salts/crypto.h>

uint8_t private_key[SALTS_CRYPTO_ED448_PRIVATE_KEY_SIZE];
uint8_t public_key[SALTS_CRYPTO_ED448_PUBLIC_KEY_SIZE];
int status = salts_crypto_ed448_keygen(private_key, public_key);
```

The elliptic-curve implementation compiles libecc's corresponding curves,
SHA-256/384/512, SHAKE256, ECDSA and EdDSA feature sets. libecc is vendored from
<https://github.com/libecc/libecc> at
commit `6e8f214f41f65d5f30b04da75472f9c24f2100db` under its BSD terms; see
`vendor/libecc/UPSTREAM.md` and `vendor/libecc/LICENSE`.

The API returns module-specific statuses so callers can distinguish invalid
signatures from operational failures. Secret intermediate state is wiped
before return. A caller owns all input and output buffers, and concurrent calls
are independent.

## Provider boundary and CHTTP migration

CHTTP's cjwt migration requires existing JOSE algorithms and key formats to
remain available. The selected dependency policy is strictly GmSSL/libecc;
adding another provider or disabling algorithms was rejected. AES Key Wrap is
implemented in the shared vcpkg-cache GmSSL overlay, using GmSSL's AES and secure
memory primitives. SaltsUtils only validates/adapts that API. No OpenSSL-shaped
compatibility target is introduced. The manifest restores GmSSL, and the static
Crypto export needs `find_dependency(GmSSL)` for final linkage. Public headers
expose no GmSSL or libecc types. libecc SHA implementation symbols are privately
prefixed because GmSSL uses some identical names with incompatible context ABIs.

The required GmSSL overlay is 3.2.0 port revision 9, based on upstream commit
`7c9f02904ef33e59c87b4f16621cc8fd434e7579`. The new `aes_key_wrap.h`,
`rsa_components.h` and `rsa_ext.h` are required;
an older provider must fail compilation rather than silently select a fallback.
The overlay is published by vcpkg-cache commit
`0dc254695917bc180504d916240292e9096e9da5`. Existing Ed448 and
SHA-256 APIs retain their ABI; new operations are additive. The header guard is
distinct from Salts Core's `cmeta_crypto.h` so both APIs can coexist.

The overlay now supplies native `gmssl/rsa_ext.h` operations: PKCS#1 v1.5 and
PSS with SHA-256/384/512, and OAEP with SHA-1/256/384/512. cjwt needs OAEP SHA-1
and SHA-256; the other two hash choices share the provider encoding and are
tested as provider capabilities. PSS signing uses an explicit salt length;
verification supports either an exact length or automatic salt recovery.
Salts key signing uses digest-length PSS salt; verification recovers the salt
length, including the maximum-length signatures in cjwt interoperability tests.

Private CRT operations use independent random base and 128-bit exponent blinding
for both factors, bounded retries, and a public-exponent result check before
releasing output. Public/private operations share fixed-width multiply/reduce
and square/multiply schedules over GmSSL's limb primitives. OAEP checks the full
decoded data block before selecting a plaintext address, returns one failure
class for ciphertext/private-operation/decoding failures, and preserves caller
output on failure. These changes keep the existing RSA key structure and function
ABI; the old SHA-256 signing/verifying entry points are regression-tested too.

An additive `RSA_COMPONENTS` ABI admits 512–8192-bit moduli, odd exponents up
to 33 bits, and private keys with only `n/e/d`. Complete CRT tuples are checked
against those components. This avoids changing the existing TLS key ABI or
requiring factor recovery. Component private operations use bounded random base
blinding, a fixed-schedule binary inverse and exponentiation, and public-result
verification before output. They do not use the CRT exponent-blinding path.
Both APIs share the PKCS#1/PSS/OAEP encodings. Small legacy RSA sizes exist only
for input compatibility; this is not a recommendation to generate such keys.

`salts_crypto_key` copies input, is immutable during synchronous/concurrent
operations, and must be destroyed after all readers return. Destruction wipes
private storage. PEM parsing uses GmSSL ASN.1/base64 and accepts unencrypted SPKI,
PKCS8, traditional RSA/EC private keys, and named or explicit parameters for the
four compiled JOSE EC groups. Explicit parameters must match libecc's full
domain parameters; they cannot construct an arbitrary curve. Compressed,
uncompressed and hybrid SEC1 points are validated. EdDSA accepts seeds; supplied
public/private pairs must agree. No encrypted PEM or password callback is added.

CHTTP now consumes these public APIs for its cjwt JWS/JWE backend. Algorithms and
valid token formats are retained; the previous PBES2 long-salt truncation is
fixed in that consumer. Reverting the migration requires restoring its backend,
dependencies and matching SDK together, with no runtime provider fallback.

**HIGH: independent security qualification remains open.** The new RSA
arithmetic, encodings and PEM parser need independent review, including
generated-code/side-channel review on each supported platform. Functional
vectors, ASan and uniform status codes do not prove constant-time execution.
The Crypto additions are distributed in SaltsUtils Native 4.3.0-rc.4 and require
the matching provider revision above.

## Verification

The formal `crypto/tests` suite covers all six
[RFC 3394 vectors](https://www.rfc-editor.org/rfc/rfc3394.txt), tampered ciphertext,
wrong KEKs, bounds, overlapping buffers and candidate-plaintext clearing. EC
tests use [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032.txt) Ed25519/Ed448 vectors
and [RFC 6979](https://www.rfc-editor.org/rfc/rfc6979.txt) public-key/signature vectors
for the three NIST curves, plus secp256k1 derivation and round trips. They reject
zero private scalars, invalid points, altered messages and invalid signatures.
The C++ test includes both Crypto headers and executes both public SHA-256 APIs.
The provider integration case links GmSSL and libecc digests in one program,
checking SHA-256/384/512 known answers. These tests establish functional compatibility for the
listed cases; they are not a side-channel audit.

RSA provider tests use independent fixtures for 2048/3072/4096-bit keys, both
CRT factor orderings, PKCS#1/PSS signatures, and OAEP ciphertexts. They check
an additional 2041-bit modulus whose PSS encoding is shorter than its RSA block,
PSS zero/maximum/automatic salt handling, malformed OAEP encodings, labels,
capacity, overlap, output preservation, CRT corruption, each random-source
failure position, and exhaustion of zero-factor retries. A separate test
executable supplies controlled randomness through the static provider's RNG
symbol; the production library has no test hook. Deterministic PSS/OAEP outputs
are checked against fixtures independently verified/decrypted by Python
`cryptography` during fixture generation. That dependency is test tooling only;
the CTest executables use GmSSL/libecc and do not link OpenSSL.

The key API tests add 1024/8192-bit RSA and a 33-bit exponent, independent
PKCS8/SPKI/traditional PEM fixtures, compressed and explicit EC parameters,
all EdDSA key imports, and independently generated symmetric known answers.
`generate_key_vectors.py` uses Python cryptography; `generate_explicit_ec_vectors.py`
uses the OpenSSL CLI only to generate public test fixtures. Neither is a build
or runtime dependency.

`python crypto/tests/generate_rsa_vectors.py --reuse-keys` refreshes fixtures
while keeping the disposable public test keys. Omitting `--reuse-keys` creates
new disposable keys; these fixture keys must never be used as credentials.

With the matching Salts 2.3 RC3 SDK and the shared vcpkg-cache environment:

```powershell
cmake --preset ci-win-release-user -DBUILD_BENCHMARKS=OFF -DSALTS_UTILS_ENABLE_CAPTURE=OFF -DVCPKG_MANIFEST_FEATURES=
cmake --build --preset ci-win-release-user --target salts_crypto_test salts_crypto_key_wrap_test salts_crypto_ec_test salts_crypto_rsa_provider_test salts_crypto_rsa_rng_failure_test salts_crypto_key_test salts_crypto_symmetric_test salts_crypto_header_cpp_test
ctest --preset ci-win-release-user -R salts_crypto --output-on-failure
```

Run the same eight CTest targets under `ci-win-dev-user` (Debug/ASan).
The restored GmSSL binary and Salts SDK themselves are not ASan-instrumented;
this does not establish sanitizer coverage inside those prebuilt libraries.
Cross-platform builds have not been qualified. cjwt's formal CHTTP suite supplies
consumer coverage separately, including independent JWS/JWE interoperability.

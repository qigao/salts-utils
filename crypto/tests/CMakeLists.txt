cmake_add_test(
  salts_crypto_test
  SOURCES salts_crypto_test.c
  LIBS Salts::Crypto Salts::TinyTest GmSSL::GmSSL
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_key_wrap_test
  SOURCES salts_crypto_key_wrap_test.c
  LIBS Salts::Crypto Salts::TinyTest
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_ec_test
  SOURCES salts_crypto_ec_test.c
  LIBS Salts::Crypto Salts::TinyTest
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_rsa_provider_test
  SOURCES salts_crypto_rsa_provider_test.c
  LIBS GmSSL::GmSSL Salts::TinyTest
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_rsa_rng_failure_test
  SOURCES salts_crypto_rsa_rng_failure_test.c
  LIBS GmSSL::GmSSL Salts::TinyTest
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_key_test
  SOURCES salts_crypto_key_test.c
  LIBS Salts::Crypto Salts::TinyTest
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_symmetric_test
  SOURCES salts_crypto_symmetric_test.c
  LIBS Salts::Crypto Salts::TinyTest
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_header_cpp_test
  SOURCES salts_crypto_header_cpp_test.cpp
  LIBS Salts::Crypto Salts::Core
  FOLDER "crypto/tests")
set_target_properties(
  salts_crypto_header_cpp_test
  PROPERTIES CXX_STANDARD 17
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF)

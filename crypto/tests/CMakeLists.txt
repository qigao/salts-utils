cmake_add_test(
  salts_crypto_test
  SOURCES salts_crypto_test.c
  LIBS Salts::Crypto Salts::TinyTest
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_header_cpp_test
  SOURCES salts_crypto_header_cpp_test.cpp
  LIBS Salts::Crypto
  FOLDER "crypto/tests")
set_target_properties(
  salts_crypto_header_cpp_test
  PROPERTIES CXX_STANDARD 17
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF)

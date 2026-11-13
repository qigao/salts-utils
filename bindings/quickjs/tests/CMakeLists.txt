cmake_add_test(salts_quickjs_cmeta_test
  SOURCES quickjs_cmeta_test.c
  LIBS Salts::QuickJS CMeta::CSTL Salts::DataBindProducer Salts::TinyTest qjs
  FOLDER "bindings/quickjs/tests")

cmake_add_test(salts_quickjs_object_test
  SOURCES quickjs_cmeta_object_test.c
  LIBS Salts::QuickJS Salts::TinyTest qjs
  FOLDER "bindings/quickjs/tests")

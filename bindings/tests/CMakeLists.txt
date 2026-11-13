cmake_add_test(salts_object_cross_runtime_test
  SOURCES object_cross_runtime_test.c
  LIBS Salts::Lua Salts::QuickJS Salts::TinyTest
  FOLDER "bindings/tests")

cmake_add_test(salts_native_module_test
  SOURCES native_module_test.c fixtures/native_math.c
  LIBS Salts::Lua Salts::QuickJS Salts::DataBindProducer Salts::TinyTest
  FOLDER "bindings/tests")

cmake_add_test(salts_native_c_frontend_test
  SOURCES native_frontend_test.c fixtures/native_math.c
  LIBS Salts::Lua Salts::QuickJS Salts::TinyTest
  FOLDER "bindings/tests")
cmake_add_test(salts_native_cpp_frontend_test
  SOURCES native_frontend_test.cpp fixtures/native_math.c fixtures/native_counter.cpp
  LIBS Salts::BindingsCpp Salts::Lua Salts::QuickJS Salts::DataBindProducer Salts::TinyTest
  FOLDER "bindings/tests")
set_tests_properties(salts_native_module_test PROPERTIES
  LABELS "bindings;cmeta;lua;quickjs;typescript")

cmake_add_test(salts_cpp_object_facade_test
  SOURCES cpp_object_facade_test.cpp
  LIBS Salts::BindingsCpp Salts::Lua Salts::QuickJS Salts::DataBindProducer Salts::TinyTest
  FOLDER "bindings/tests")

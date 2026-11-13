cmake_add_test(salts_cpp_member_thunk_test
  SOURCES cpp_member_thunk_test.cpp
  LIBS Salts::BindingsCpp Salts::Lua Salts::QuickJS Salts::DataBindProducer Salts::TinyTest
  FOLDER "bindings/cpp/tests")

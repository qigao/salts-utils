cmake_add_test(c11_lua_bind_test
  SOURCES c11_lua_bind_test.c
  LIBS c11_lua_bind Salts::TinyTest
  FOLDER "tools/lua/tests")

set_target_properties(c11_lua_bind_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)

cmake_add_test(cpp_lua_bind_test
  SOURCES cpp_lua_bind_test.cpp ../salts_lua.hpp
  LIBS Salts::LuaBind Salts::TinyTest
  FOLDER "tools/lua/tests")

set_target_properties(cpp_lua_bind_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)

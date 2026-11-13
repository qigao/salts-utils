cmake_add_test(salts_lua_invokable_test
  SOURCES lua_cmeta_invokable_test.c
  LIBS Salts::Lua Salts::TinyTest
  FOLDER "bindings/lua/tests")

cmake_add_test(salts_lua_object_test
  SOURCES lua_cmeta_object_test.c
  LIBS Salts::Lua Salts::TinyTest
  FOLDER "bindings/lua/tests")

cmake_add_test(salts_lua_conversion_test
  SOURCES lua_cmeta_conversion_test.c
  LIBS Salts::Lua Salts::TinyTest
  FOLDER "bindings/lua/tests")

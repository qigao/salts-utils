add_library(cpp_lua_bind INTERFACE)
target_include_directories(cpp_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
if(TARGET Lua::Lua)
  target_link_libraries(cpp_lua_bind INTERFACE Lua::Lua)
else()
  target_include_directories(cpp_lua_bind INTERFACE ${LUA_INCLUDE_DIR})
  target_link_libraries(cpp_lua_bind INTERFACE ${LUA_LIBRARIES})
endif()

cmake_add_test(cpp_lua_bind_test
  SOURCES cpp_lua_bind_test.cpp cpp_lua_bind.hpp ../turbo_lua_bind.hpp
  LIBS TurboParser::LuaBind TurboUtils::TinyTest
  FOLDER "tools/lua/cpp")

set_target_properties(cpp_lua_bind_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)

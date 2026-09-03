find_package(Lua REQUIRED)

add_library(c11_lua_bind INTERFACE)
target_include_directories(c11_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c11_lua_bind INTERFACE Salts::Core Salts::CMeta)

if(TARGET Lua::Lua)
  target_link_libraries(c11_lua_bind INTERFACE Lua::Lua)
else()
  target_include_directories(c11_lua_bind INTERFACE ${LUA_INCLUDE_DIR})
  target_link_libraries(c11_lua_bind INTERFACE ${LUA_LIBRARIES})
endif()

add_library(cpp_lua_bind INTERFACE)
target_include_directories(cpp_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
if(TARGET Lua::Lua)
  target_link_libraries(cpp_lua_bind INTERFACE Lua::Lua)
else()
  target_include_directories(cpp_lua_bind INTERFACE ${LUA_INCLUDE_DIR})
  target_link_libraries(cpp_lua_bind INTERFACE ${LUA_LIBRARIES})
endif()

add_library(salts_lua_bind INTERFACE)
target_include_directories(salts_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(salts_lua_bind INTERFACE c11_lua_bind cpp_lua_bind)
add_library(Salts::LuaBind ALIAS salts_lua_bind)

add_subdirectory(tests)

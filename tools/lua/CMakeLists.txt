find_package(Lua REQUIRED)

add_library(c11_lua_bind INTERFACE)
target_include_directories(c11_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c11_lua_bind INTERFACE Rocida::Core Rocida::CMeta)

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

add_library(turbo_lua_bind INTERFACE)
target_include_directories(turbo_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(turbo_lua_bind INTERFACE c11_lua_bind cpp_lua_bind)
add_library(TurboParser::LuaBind ALIAS turbo_lua_bind)

add_subdirectory(tests)

find_package(Lua REQUIRED)

add_library(c11_lua_bind INTERFACE)
target_include_directories(c11_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c11_lua_bind INTERFACE TurboUtils::Core)

if(TARGET Lua::Lua)
  target_link_libraries(c11_lua_bind INTERFACE Lua::Lua)
else()
  target_include_directories(c11_lua_bind INTERFACE ${LUA_INCLUDE_DIR})
  target_link_libraries(c11_lua_bind INTERFACE ${LUA_LIBRARIES})
endif()

add_library(c11_lua_data_bind INTERFACE)
target_include_directories(c11_lua_data_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c11_lua_data_bind INTERFACE c11_lua_bind TurboParser::DataBind)

add_library(c11_lua_typed INTERFACE)
target_include_directories(c11_lua_typed INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c11_lua_typed INTERFACE c11_lua_data_bind TurboParser::DataBind)

add_library(turbo_lua_executor STATIC turbo_lua_executor.c)
target_include_directories(turbo_lua_executor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(turbo_lua_executor PUBLIC TurboUtils::Core)
add_library(TurboParser::LuaExecutor ALIAS turbo_lua_executor)

add_library(turbo_lua_worker STATIC turbo_lua_worker.c)
target_include_directories(turbo_lua_worker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(turbo_lua_worker PUBLIC turbo_lua_executor c11_lua_bind)
add_library(TurboParser::LuaWorker ALIAS turbo_lua_worker)

add_library(turbo_lua_bind INTERFACE)
target_include_directories(turbo_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(turbo_lua_bind INTERFACE c11_lua_typed cpp_lua_bind turbo_lua_worker)
add_library(TurboParser::LuaBind ALIAS turbo_lua_bind)

cmake_add_test(c11_lua_bind_test
  SOURCES c11_lua_bind_test.c
  LIBS c11_lua_bind TurboUtils::TinyTest
  FOLDER "tools/lua")

cmake_add_test(c11_lua_data_bind_test
  SOURCES c11_lua_data_bind_test.c
  LIBS c11_lua_data_bind TurboUtils::TinyTest
  FOLDER "tools/lua")

cmake_add_test(c11_lua_typed_test
  SOURCES c11_lua_typed_test.c turbo_lua_bind.h
  LIBS turbo_lua_bind TurboUtils::TinyTest
  FOLDER "tools/lua")

cmake_add_test(turbo_lua_executor_test
  SOURCES turbo_lua_executor_test.c
  LIBS turbo_lua_executor c11_lua_bind TurboUtils::TinyTest
  FOLDER "tools/lua")

cmake_add_test(turbo_lua_worker_test
  SOURCES turbo_lua_worker_test.c
  LIBS turbo_lua_worker TurboUtils::TinyTest
  FOLDER "tools/lua")

set_target_properties(c11_lua_bind_test c11_lua_data_bind_test c11_lua_typed_test
  turbo_lua_executor_test turbo_lua_worker_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)

add_subdirectory(cpp)

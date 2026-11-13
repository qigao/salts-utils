find_package(unofficial-lua CONFIG REQUIRED
  PATHS "${SALTS_UTILS_DEPENDENCY_PREFIX}/share/unofficial-lua"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

add_library(salts_lua_binding STATIC salts_lua_cmeta.c salts_lua_module.c)
add_library(Salts::Lua ALIAS salts_lua_binding)
set_target_properties(salts_lua_binding PROPERTIES EXPORT_NAME Lua)

target_include_directories(salts_lua_binding
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(salts_lua_binding
  PUBLIC Salts::Bindings lua)

target_compile_features(salts_lua_binding PUBLIC c_std_11)
set_target_properties(salts_lua_binding PROPERTIES FOLDER "bindings")

install(TARGETS salts_lua_binding EXPORT SaltsUtilsTargets)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

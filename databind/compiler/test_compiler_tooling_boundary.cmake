cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED DATABIND_SOURCE_ROOT OR "${DATABIND_SOURCE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "DATABIND_SOURCE_ROOT is required")
endif()
if(NOT IS_DIRECTORY "${DATABIND_SOURCE_ROOT}")
  message(FATAL_ERROR "DATABIND_SOURCE_ROOT is not a directory: ${DATABIND_SOURCE_ROOT}")
endif()

file(READ "${DATABIND_SOURCE_ROOT}/compiler/CMakeLists.txt" COMPILER_CMAKE)

string(FIND "${COMPILER_CMAKE}"
  "set(DATABIND_COMPILER_TOOLING_TARGET databind_compiler_tooling)" TOOLING_TARGET_POS)
if(TOOLING_TARGET_POS EQUAL -1)
  message(FATAL_ERROR
    "DataBind compiler must declare an explicit build-tool dependency boundary")
endif()

string(FIND "${COMPILER_CMAKE}"
  "target_link_libraries(\${DATABIND_COMPILER_TOOLING_TARGET}" TOOLING_LINK_POS)
if(TOOLING_LINK_POS EQUAL -1)
  message(FATAL_ERROR "Compiler tooling boundary has no explicit link contract")
endif()

foreach(REQUIRED IN ITEMS
        "Salts::CmdParser"
        "Salts::Mustache"
        "Salts::JsonParser")
  string(FIND "${COMPILER_CMAKE}" "${REQUIRED}" REQUIRED_POS)
  if(REQUIRED_POS EQUAL -1)
    message(FATAL_ERROR "Compiler tooling boundary lost required helper: ${REQUIRED}")
  endif()
endforeach()

foreach(DIRECT IN ITEMS
        "Salts::CmdParser"
        "Salts::Mustache"
        "Salts::JsonParser")
  string(FIND "${COMPILER_CMAKE}"
    "LIBS Salts::DataBindSchema ${DIRECT}" DIRECT_LINK_POS)
  if(NOT DIRECT_LINK_POS EQUAL -1)
    message(FATAL_ERROR
      "databindc directly owns compiler helper instead of tooling boundary: ${DIRECT}")
  endif()
endforeach()

string(FIND "${COMPILER_CMAKE}"
  "Salts::PluginABI \${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core"
  BOUNDARY_USE_POS)
if(BOUNDARY_USE_POS EQUAL -1)
  message(FATAL_ERROR
    "databindc does not consume the explicit compiler tooling boundary")
endif()

get_filename_component(SALTS_UTILS_SOURCE_ROOT "${DATABIND_SOURCE_ROOT}" DIRECTORY)
foreach(RETIRED_PATH IN ITEMS
        "${SALTS_UTILS_SOURCE_ROOT}/tools/lua"
        "${DATABIND_SOURCE_ROOT}/compiler/templates/c_lua_bind.mustache"
        "${DATABIND_SOURCE_ROOT}/compiler/test_typed_order_lua.c")
  if(EXISTS "${RETIRED_PATH}")
    message(FATAL_ERROR
      "Retired schema-specific Lua binding path was restored: ${RETIRED_PATH}")
  endif()
endforeach()

foreach(COMPILER_FILE IN ITEMS
        "${DATABIND_SOURCE_ROOT}/compiler/CMakeLists.txt"
        "${DATABIND_SOURCE_ROOT}/compiler/compiler_core.c"
        "${DATABIND_SOURCE_ROOT}/compiler/compiler_core.h"
        "${DATABIND_SOURCE_ROOT}/compiler/main.c"
        "${DATABIND_SOURCE_ROOT}/compiler/projection_frontend.c"
        "${DATABIND_SOURCE_ROOT}/compiler/projection_frontend.h"
        "${DATABIND_SOURCE_ROOT}/compiler/templates/c_structs.mustache"
        "${DATABIND_SOURCE_ROOT}/compiler/templates/c_typed_source.mustache")
  file(READ "${COMPILER_FILE}" COMPILER_CONTENT)
  foreach(RETIRED_MARKER IN ITEMS
          "lua_output_path"
          "--lua-output"
          "lua_output_enabled"
          "has_lua_bindings"
          "salts_lua.h"
          "Salts::LuaBind")
    string(FIND "${COMPILER_CONTENT}" "${RETIRED_MARKER}" RETIRED_POS)
    if(NOT RETIRED_POS EQUAL -1)
      message(FATAL_ERROR
        "Retired schema-specific Lua binding marker ${RETIRED_MARKER} remains in ${COMPILER_FILE}")
    endif()
  endforeach()
endforeach()

message(STATUS "DataBind compiler tooling boundary passed")

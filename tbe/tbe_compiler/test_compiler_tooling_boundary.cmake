file(READ "${PROJECT_SOURCE_DIR}/tbe/tbe_compiler/CMakeLists.txt" COMPILER_CMAKE)

string(FIND "${COMPILER_CMAKE}"
  "set(DATABIND_COMPILER_TOOLING_TARGET databind_compiler_tooling)" TOOLING_TARGET_POS)
if(TOOLING_TARGET_POS EQUAL -1)
  message(FATAL_ERROR
    "DataBind compiler must declare an explicit build-tool dependency boundary")
endif()

string(FIND "${COMPILER_CMAKE}"
  "target_link_libraries(${DATABIND_COMPILER_TOOLING_TARGET}" TOOLING_LINK_POS)
if(TOOLING_LINK_POS EQUAL -1)
  message(FATAL_ERROR "Compiler tooling boundary has no explicit link contract")
endif()

foreach(REQUIRED IN ITEMS "Salts::CmdParser" "Salts::Mustache")
  string(FIND "${COMPILER_CMAKE}" "${REQUIRED}" REQUIRED_POS)
  if(REQUIRED_POS EQUAL -1)
    message(FATAL_ERROR "Compiler tooling boundary lost required helper: ${REQUIRED}")
  endif()
endforeach()

string(FIND "${COMPILER_CMAKE}"
  "LIBS Salts::TbeSchema Salts::CmdParser Salts::Mustache Salts::Core" DIRECT_LINK_POS)
if(NOT DIRECT_LINK_POS EQUAL -1)
  message(FATAL_ERROR
    "tbe_compiler still directly owns SaltsUtils CmdParser/Mustache dependencies")
endif()

string(FIND "${COMPILER_CMAKE}"
  "LIBS Salts::TbeSchema ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core" BOUNDARY_USE_POS)
if(BOUNDARY_USE_POS EQUAL -1)
  message(FATAL_ERROR
    "tbe_compiler does not consume the explicit compiler tooling boundary")
endif()

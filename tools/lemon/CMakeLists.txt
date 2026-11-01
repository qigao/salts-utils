if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.23)
  project(SaltsUtilsLemonHost LANGUAGES C)
endif()

set(_salts_utils_target_runs_on_host FALSE)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL CMAKE_SYSTEM_NAME AND
   (NOT CMAKE_SYSTEM_PROCESSOR OR
    CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL CMAKE_SYSTEM_PROCESSOR))
  # The shared native LLVM toolchain deliberately sets CMAKE_SYSTEM_NAME but
  # leaves CMAKE_SYSTEM_PROCESSOR unset. llvm-mingw sets both. In either case
  # the generated executable is host-runnable when the known fields match.
  set(_salts_utils_target_runs_on_host TRUE)
endif()

if(CMAKE_CROSSCOMPILING AND NOT _salts_utils_target_runs_on_host)
  if(NOT SALTS_UTILS_HOST_LEMON_EXECUTABLE OR
     NOT EXISTS "${SALTS_UTILS_HOST_LEMON_EXECUTABLE}" OR
     IS_DIRECTORY "${SALTS_UTILS_HOST_LEMON_EXECUTABLE}")
    message(FATAL_ERROR
      "Cross-compiling requires SALTS_UTILS_HOST_LEMON_EXECUTABLE from a completed host preset build")
  endif()
  add_executable(lemon IMPORTED GLOBAL)
  set_target_properties(lemon PROPERTIES
    IMPORTED_LOCATION "${SALTS_UTILS_HOST_LEMON_EXECUTABLE}")
else()
  add_executable(lemon lemon.c)
  target_compile_features(lemon PRIVATE c_std_11)
  set_target_properties(lemon PROPERTIES FOLDER tools)
endif()
unset(_salts_utils_target_runs_on_host)

add_executable(Salts::Lemon ALIAS lemon)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.23)
  project(SaltsUtilsLemonHost LANGUAGES C)
endif()

if(CMAKE_CROSSCOMPILING)
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

add_executable(Salts::Lemon ALIAS lemon)

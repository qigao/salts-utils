include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# Address Sanitizer - only enabled for Debug builds
cmake_dependent_option(ENABLE_ASAN "Enable Address Sanitizer" ON
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)

# if(MSVC) add_compile_options(/bigobj) endif()

option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test suite" ON)
set(SALTS_UTILS_HOST_LEMON_EXECUTABLE "" CACHE FILEPATH
    "Host Lemon executable required while cross-compiling")
cmake_dependent_option(BUILD_BENCHMARKS "Build benchmark executables" ON
                       "BUILD_TESTS" OFF)


if(WIN32
   OR ANDROID
   OR APPLE
   OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_SALTS_UTILS_CAPTURE_DEFAULT ON)
else()
  set(_SALTS_UTILS_CAPTURE_DEFAULT OFF)
endif()
option(SALTS_UTILS_ENABLE_CAPTURE
       "Build the paired native capture and playback components"
       ${_SALTS_UTILS_CAPTURE_DEFAULT})

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# building the tests
option(ENABLE_TESTS "Enable the tests" ON)

# Address Sanitizer - only enabled for Debug builds
cmake_dependent_option(ENABLE_ASAN "Enable Address Sanitizer" ON
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)


# if(MSVC) add_compile_options(/bigobj) endif()

option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test suite" ON)
cmake_dependent_option(BUILD_BENCHMARKS "Build benchmark executables" ON
                       "BUILD_TESTS" OFF)

option(SALTS_UTILS_ENABLE_CAPTURE
       "Build the optional native audio/video/screen capture component" OFF)
if(WIN32)
  set(SALTS_UTILS_ENABLE_CAPTURE ON CACHE BOOL
      "Build the native audio/video/screen capture component" FORCE)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

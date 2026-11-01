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

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

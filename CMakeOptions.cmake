include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# Address Sanitizer - only enabled for Debug builds
cmake_dependent_option(ENABLE_ASAN "Enable Address Sanitizer" ON
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)

option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test suite" ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

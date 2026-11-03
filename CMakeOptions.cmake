set(CMAKE_COLOR_DIAGNOSTICS ON)

option(ENABLE_SANITIZER_ADDRESS "Enable AddressSanitizer" OFF)
option(ENABLE_SANITIZER_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_SANITIZER_LEAK "Enable LeakSanitizer" OFF)
option(ENABLE_SANITIZER_THREAD "Enable ThreadSanitizer" OFF)
option(ENABLE_SANITIZER_MEMORY "Enable MemorySanitizer (Clang only)" OFF)

option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test suite" ON)
set(SALTS_UTILS_HOST_LEMON_EXECUTABLE "" CACHE FILEPATH
    "Host Lemon executable required while cross-compiling")
option(BUILD_BENCHMARKS "Build benchmark executables" ${BUILD_TESTS})
if(BUILD_BENCHMARKS AND NOT BUILD_TESTS)
  message(FATAL_ERROR "BUILD_BENCHMARKS requires BUILD_TESTS")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

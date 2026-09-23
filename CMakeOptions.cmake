include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# Address Sanitizer - only enabled for Debug builds
cmake_dependent_option(ENABLE_ASAN "Enable Address Sanitizer" ON
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)


# if(MSVC) add_compile_options(/bigobj) endif()

option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test suite" ON)
cmake_dependent_option(BUILD_BENCHMARKS "Build benchmark executables" ON
                       "BUILD_TESTS" OFF)

if(WIN32)
  set(_SALTS_UTILS_CAPTURE_DEFAULT ON)
else()
  set(_SALTS_UTILS_CAPTURE_DEFAULT OFF)
endif()
option(SALTS_UTILS_ENABLE_CAPTURE
       "Build the optional native audio/video/screen capture component"
       ${_SALTS_UTILS_CAPTURE_DEFAULT})
unset(_SALTS_UTILS_CAPTURE_DEFAULT)

option(SALTS_UTILS_ENABLE_CFLOW_USB
       "Build the optional libusb-backed CFlow device adapter" OFF)

option(SALTS_UTILS_QUALIFY_PLUGIN_DATABIND
       "Internal focused Plugin/DataBind qualification profile" OFF)
mark_as_advanced(SALTS_UTILS_QUALIFY_PLUGIN_DATABIND)

if(SALTS_UTILS_QUALIFY_PLUGIN_DATABIND AND
   (SALTS_UTILS_ENABLE_CAPTURE OR SALTS_UTILS_ENABLE_CFLOW_USB))
  message(FATAL_ERROR
    "The focused Plugin/DataBind qualification profile excludes capture and USB")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

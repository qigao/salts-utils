include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# Address Sanitizer - only enabled for Debug builds
cmake_dependent_option(ENABLE_ASAN "Enable Address Sanitizer" ON
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)


# if(MSVC) add_compile_options(/bigobj) endif()

option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test suite" ON)
option(SALTS_UTILS_BUILD_IDLC
       "Build and install the host salts-idlc compiler executable" ON)
cmake_dependent_option(BUILD_BENCHMARKS "Build benchmark executables" ON
                       "BUILD_TESTS" OFF)

if(BUILD_TESTS AND NOT SALTS_UTILS_BUILD_IDLC)
  message(FATAL_ERROR
    "BUILD_TESTS requires SALTS_UTILS_BUILD_IDLC because compiler tests execute salts-idlc")
endif()

if(WIN32 OR ANDROID OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
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

option(SALTS_UTILS_QUALIFY_DATABIND
       "Internal focused DataBind qualification profile" OFF)
mark_as_advanced(SALTS_UTILS_QUALIFY_DATABIND)

option(SALTS_UTILS_QUALIFY_BINDINGS
       "Internal focused language-binding qualification profile" OFF)
mark_as_advanced(SALTS_UTILS_QUALIFY_BINDINGS)

if(SALTS_UTILS_QUALIFY_DATABIND AND
   (SALTS_UTILS_ENABLE_CAPTURE OR SALTS_UTILS_ENABLE_CFLOW_USB))
  message(FATAL_ERROR
    "The focused DataBind qualification profile excludes capture and USB")
endif()

if(SALTS_UTILS_QUALIFY_BINDINGS AND
   (SALTS_UTILS_ENABLE_CAPTURE OR SALTS_UTILS_ENABLE_CFLOW_USB))
  message(FATAL_ERROR
    "The focused bindings qualification profile excludes capture and USB")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

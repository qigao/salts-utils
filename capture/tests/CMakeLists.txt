add_library(
  capture_common_test_impl STATIC
  ../src/capture_video.c
  fake_capture_backend.c)
cmake_config_target(
  capture_common_test_impl
  FOLDER "capture/tests")
target_compile_features(capture_common_test_impl PUBLIC c_std_11)
target_include_directories(
  capture_common_test_impl
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

cmake_add_test(
  capture_contract_test
  SOURCES test_capture_contract.c
  LIBS capture_common_test_impl Salts::TinyTest
  FOLDER "capture/tests")

if(WIN32)
  # Exercise the real Windows control entry points with a fake COM device;
  # including the backend keeps its private capture context out of the SDK ABI.
  add_executable(capture_exposure_windows_test test_capture_exposure_windows.c)
  target_compile_definitions(capture_exposure_windows_test PRIVATE SALTS_CAPTURE_BUILD)
  target_link_libraries(capture_exposure_windows_test PRIVATE
    Salts::Capture Salts::Core Salts::TinyTest yuv
    mfplat mf mfuuid mfreadwrite strmiids ole32 oleaut32)
  add_test(NAME capture_exposure_windows_test COMMAND capture_exposure_windows_test)
  set_target_properties(capture_exposure_windows_test PROPERTIES FOLDER "capture/tests")
  cmake_add_test(
    capture_windows_test
    SOURCES test_capture_windows.c
    LIBS Salts::Capture Salts::TinyTest
    RUNTIME_TARGETS yuv
    FOLDER "capture/tests")
endif()

if(ANDROID)
  cmake_add_test(
    capture_android_test
    SOURCES test_capture_android.c
    LIBS Salts::Capture Salts::TinyTest
    FOLDER "capture/tests")
endif()

if(UNIX AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  cmake_add_test(
    capture_posix_test
    SOURCES test_capture_posix.c
    LIBS Salts::Capture Salts::TinyTest
    FOLDER "capture/tests")
endif()

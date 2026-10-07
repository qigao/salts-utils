# Installed-SDK consumers keep their own configure and dependency scopes.
if(CMAKE_HOST_WIN32)
  set(package_config_preset win-release-user)
else()
  set(package_config_preset ci-native-release-user)
endif()

foreach(consumer IN ITEMS databind_public databind_target wasm_execution)
  add_custom_target(databind_package_config_${consumer}
    COMMAND "${CMAKE_COMMAND}" --preset "${package_config_preset}"
    COMMAND "${CMAKE_COMMAND}" --build --preset "${package_config_preset}"
    COMMAND "${CMAKE_CTEST_COMMAND}" --preset "${package_config_preset}"
      --output-on-failure
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${consumer}"
    USES_TERMINAL
    VERBATIM)
  cmake_config_target(databind_package_config_${consumer}
    FOLDER "databind/compiler/package_config/${consumer}")
endforeach()

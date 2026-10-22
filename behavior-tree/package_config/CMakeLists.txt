cmake_minimum_required(VERSION 3.27)
project(BehaviorTreeInstalledConsumer LANGUAGES C)

include(CTest)

if(NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
  message(FATAL_ERROR "SALTS_UTILS_ROOT must name the installed SaltsUtils SDK")
endif()

unset(SaltsUtils_DIR CACHE)
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "$ENV{SALTS_UTILS_ROOT}/lib/cmake/SaltsUtils"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

if(NOT TARGET Salts::BehaviorTree)
  message(FATAL_ERROR "installed SaltsUtils package is missing Salts::BehaviorTree")
endif()

add_executable(behavior_tree_installed_consumer consumer.c)
target_compile_features(behavior_tree_installed_consumer PRIVATE c_std_11)
set_target_properties(behavior_tree_installed_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_link_libraries(behavior_tree_installed_consumer PRIVATE Salts::BehaviorTree)
add_test(NAME behavior_tree_installed_consumer
  COMMAND behavior_tree_installed_consumer)

add_subdirectory(common)
add_subdirectory(cpp)

add_subdirectory(lua)

add_subdirectory(quickjs)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

cmake_add_test(cflow_process_test
  SOURCES cflow_process_test.c
  LIBS Salts::Process Salts::Platform Salts::TinyTest
  FOLDER "cflow-process/tests")
set_target_properties(cflow_process_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_process_header_cpp_test
  SOURCES cflow_process_header_cpp_test.cpp
  LIBS Salts::Process Salts::TinyTest
  FOLDER "cflow-process/tests")
set_target_properties(cflow_process_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

# Test-only source composition: compile the unmodified Process implementation
# with three private outbound status shims. This does not instrument the
# installed Salts::Process target or extend its public ABI.
cmake_add_test(cflow_process_fault_test
  SOURCES cflow_process_fault_test.c
  LIBS Salts::Core Salts::CFlow Salts::Platform Salts::TinyTest
  FOLDER "cflow-process/tests")
target_include_directories(cflow_process_fault_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include")
set_target_properties(cflow_process_fault_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

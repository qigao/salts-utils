# #99 migration prerequisites, using the real existing DataBind implementation.
add_executable(test_data_bind_native_storage_requirements native_storage_requirements.c)
target_link_libraries(test_data_bind_native_storage_requirements
  PRIVATE Salts::DataBind Salts::Core Salts::TinyTest)
set_target_properties(test_data_bind_native_storage_requirements PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  LINKER_LANGUAGE CXX FOLDER "databind/tests")
if(MSVC)
  target_compile_options(test_data_bind_native_storage_requirements PRIVATE /W4 /WX /Zc:preprocessor)
else()
  target_compile_options(test_data_bind_native_storage_requirements PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_data_bind_native_storage_requirements
  COMMAND test_data_bind_native_storage_requirements)
set_tests_properties(test_data_bind_native_storage_requirements PROPERTIES TIMEOUT 60)

# Independent source controls, not proof of a DataBind direct-reader decoder.
add_executable(test_data_bind_reader_probe reader_probe_test.c reader_probe.h)
target_link_libraries(test_data_bind_reader_probe
  PRIVATE Salts::CSerde Salts::TinyTest)
set_target_properties(test_data_bind_reader_probe PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  LINKER_LANGUAGE CXX FOLDER "databind/tests")
if(MSVC)
  target_compile_options(test_data_bind_reader_probe PRIVATE /W4 /WX /Zc:preprocessor)
else()
  target_compile_options(test_data_bind_reader_probe PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_data_bind_reader_probe COMMAND test_data_bind_reader_probe)
set_tests_properties(test_data_bind_reader_probe PROPERTIES TIMEOUT 60)

# #99 interface-first consumers: intentionally no fallback header or test decoder.
add_executable(test_data_bind_native_reader native_reader_contract_test.c
  native_reader_preflight_test.c native_reader_bitmap_test.c)
add_executable(test_data_bind_native_reader_cpp native_reader_cpp_contract.cpp)
add_executable(test_data_bind_native_lifecycle native_lifecycle_contract_test.c)
add_executable(test_data_bind_native_enum native_enum_contract_test.c)
add_executable(test_data_bind_native_writer native_writer_contract_test.c)
add_executable(test_data_bind_native_container native_container_contract_test.c)
add_executable(test_data_bind_native_variant native_variant_contract_test.c)
foreach(target IN ITEMS
    test_data_bind_native_reader
    test_data_bind_native_reader_cpp
    test_data_bind_native_lifecycle
    test_data_bind_native_enum
    test_data_bind_native_writer
    test_data_bind_native_container
    test_data_bind_native_variant)
  target_link_libraries(${target} PRIVATE Salts::DataBindCore Salts::Core)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    LINKER_LANGUAGE CXX FOLDER "databind/tests")
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /Zc:preprocessor)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES TIMEOUT 60)
endforeach()
target_link_libraries(test_data_bind_native_reader PRIVATE Salts::TinyTest)
target_link_libraries(test_data_bind_native_lifecycle PRIVATE Salts::TinyTest)
target_link_libraries(test_data_bind_native_enum PRIVATE Salts::TinyTest Salts::DataBind)
cmake_set_test_runtime_path(test_data_bind_native_enum test_data_bind_native_enum)
target_link_libraries(test_data_bind_native_writer PRIVATE Salts::TinyTest)
target_link_libraries(test_data_bind_native_container
  PRIVATE Salts::TinyTest Salts::CSTL)
target_link_libraries(test_data_bind_native_variant
  PRIVATE Salts::TinyTest)

# Pull the real salts-idlc-generated tstr/stl_byte_buffer lifecycle proof into
# the existing native-writer producer gate. The fixture is generated and run
# only for host builds, matching the runtime CMake boundary.
if(TARGET test_databind_generated_owned_buffers)
  add_dependencies(test_data_bind_native_writer
                   test_databind_generated_owned_buffers)
  set_tests_properties(test_data_bind_native_writer PROPERTIES
    FIXTURES_REQUIRED databind_generated_owned_buffers_contract)
endif()

cmake_add_test(test_data_bind_native_scalar
  SOURCES native_scalar_contract_test.c
  LIBS Salts::DataBind Salts::TinyTest
  FOLDER "databind/tests")
set_tests_properties(test_data_bind_native_scalar PROPERTIES
  LABELS "databind;cmeta;native;scalar" TIMEOUT 60)

cmake_add_test(test_data_bind_native_message
  SOURCES native_message_contract_test.c
  LIBS Salts::DataBind Salts::TinyTest
  FOLDER "databind/tests")
set_tests_properties(test_data_bind_native_message PROPERTIES
  LABELS "databind;cmeta;native;message" TIMEOUT 60)

cmake_add_test(test_data_bind_native_collection_message
  SOURCES native_collection_message_test.c
  LIBS Salts::DataBind Salts::CSTL Salts::TinyTest
  FOLDER "databind/tests")
set_tests_properties(test_data_bind_native_collection_message PROPERTIES
  LABELS "databind;cmeta;native;collection" TIMEOUT 60)

cmake_add_test(test_data_bind_native_ownership_boundary
  SOURCES native_ownership_boundary_test.c
  LIBS Salts::DataBind Salts::TinyTest
  FOLDER "databind/tests")
set_tests_properties(test_data_bind_native_ownership_boundary PROPERTIES
  LABELS "databind;cmeta;native;ownership" TIMEOUT 60)

cmake_add_test(test_data_bind_native_bytes_json
  SOURCES native_bytes_json_test.c
  LIBS Salts::DataBind Salts::CSTL Salts::TinyTest
  FOLDER "databind/tests")
set_tests_properties(test_data_bind_native_bytes_json PROPERTIES
  LABELS "databind;cmeta;native;bytes;json" TIMEOUT 60)

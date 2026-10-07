# Canonical native storage admission.
add_executable(test_data_bind_native_storage_requirements native_storage_requirements.c)
target_link_libraries(test_data_bind_native_storage_requirements
  PRIVATE Salts::DataBind Salts::Core Salts::TinyTest)
set_target_properties(test_data_bind_native_storage_requirements PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  LINKER_LANGUAGE CXX)
cmake_config_target(test_data_bind_native_storage_requirements
  FOLDER "databind/tests")
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
  LINKER_LANGUAGE CXX)
cmake_config_target(test_data_bind_reader_probe
  FOLDER "databind/tests")
if(MSVC)
  target_compile_options(test_data_bind_reader_probe PRIVATE /W4 /WX /Zc:preprocessor)
else()
  target_compile_options(test_data_bind_reader_probe PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_data_bind_reader_probe COMMAND test_data_bind_reader_probe)
set_tests_properties(test_data_bind_reader_probe PROPERTIES TIMEOUT 60)

add_executable(test_data_bind_native_control_records native_control_record_test.c)
target_link_libraries(test_data_bind_native_control_records
  PRIVATE Salts::DataBind Salts::Core Salts::TinyTest)
set_target_properties(test_data_bind_native_control_records PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
  LINKER_LANGUAGE CXX)
cmake_config_target(test_data_bind_native_control_records
  FOLDER "databind/tests")
if(MSVC)
  target_compile_options(test_data_bind_native_control_records PRIVATE /W4 /WX /Zc:preprocessor)
else()
  target_compile_options(test_data_bind_native_control_records PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_data_bind_native_control_records
  COMMAND test_data_bind_native_control_records)
set_tests_properties(test_data_bind_native_control_records PROPERTIES
  LABELS "databind;cmeta;native;abi" TIMEOUT 60)

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

cmake_add_test(test_data_bind_native_value_reader
  SOURCES native_value_reader_test.c
  LIBS Salts::DataBind Salts::CSTL Salts::TinyTest
  FOLDER "databind/tests")
set_tests_properties(test_data_bind_native_value_reader PROPERTIES
  LABELS "databind;cmeta;native;dynamic;ownership" TIMEOUT 60)

cmake_add_test(test_data_bind_native_value_reader_cpp
  SOURCES native_value_reader_cpp_test.cpp
  LIBS Salts::DataBind Salts::TinyTest
  FOLDER "databind/tests")
set_tests_properties(test_data_bind_native_value_reader_cpp PROPERTIES
  LABELS "databind;cmeta;native;dynamic;public;cpp" TIMEOUT 60)

cmake_minimum_required(VERSION 3.20)
project(xml_private_symbols LANGUAGES C)
include(CTest)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This regression requires the Linux ELF loader and nm")
endif()
find_package(Salts CONFIG REQUIRED)
find_package(SaltsUtils CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_target_property(xml_parser_type Salts::XmlParser TYPE)
if(NOT xml_parser_type STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "The XML private-engine regression requires the installed static facade")
endif()
add_executable(xml_multi_library multi_library.c)
target_compile_features(xml_multi_library PRIVATE c_std_11)
set_target_properties(xml_multi_library PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(xml_multi_library PRIVATE Salts::XmlParser ${CMAKE_DL_LIBS})
target_compile_options(xml_multi_library PRIVATE -Wall -Wextra -Wpedantic -Werror
  -g -fno-omit-frame-pointer -fsanitize=address,undefined)
target_link_options(xml_multi_library PRIVATE -fsanitize=address,undefined)
add_test(NAME xml_private_databind_first COMMAND xml_multi_library
  $<TARGET_FILE:Salts::Databind> $<TARGET_FILE:Salts::Mustache>)
add_test(NAME xml_private_mustache_first COMMAND xml_multi_library
  $<TARGET_FILE:Salts::Mustache> $<TARGET_FILE:Salts::Databind>)
add_test(NAME xml_private_export_table COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/verify_exports.py ${CMAKE_NM}
  $<TARGET_FILE:Salts::XmlParser>
  $<TARGET_FILE:Salts::Databind> $<TARGET_FILE:Salts::Mustache>)

cmake_add_test(test_salts_unicode
  SOURCES test_salts_unicode.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")

cmake_add_test(test_salts_unicode_header_cpp
  SOURCES test_salts_unicode_header_cpp.cpp
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")

set_tests_properties(test_salts_unicode PROPERTIES LABELS "unicode;unit")
set_tests_properties(test_salts_unicode_header_cpp PROPERTIES LABELS "unicode;headers")

cmake_add_test(test_salts_unicode_grapheme
  SOURCES test_salts_unicode_grapheme.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
set_tests_properties(test_salts_unicode_grapheme PROPERTIES LABELS "unicode;grapheme")

cmake_add_test(test_salts_unicode_grapheme_conformance
  SOURCES test_salts_unicode_grapheme_conformance.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
target_compile_definitions(test_salts_unicode_grapheme_conformance
  PRIVATE SALTS_UNICODE_GRAPHEME_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../data/17.0.0/GraphemeBreakTest.txt")
set_tests_properties(test_salts_unicode_grapheme_conformance
  PROPERTIES LABELS "unicode;grapheme;conformance")

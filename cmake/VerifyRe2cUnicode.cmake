# Unicode 17.0.0 include files from the official re2c 4.6 release.
# Canonical SHA-256 uses LF text; Windows packages may carry CRLF line endings.
# Never rewrite installed data, trim whitespace, or accept different character sets.
# NEW evaluation preserves embedded bytes in script mode rather than allowing
# legacy expansion to truncate at NUL. Keep the policy local to this module.
cmake_policy(PUSH)
cmake_policy(SET CMP0053 NEW)

function(salts_utils_verify_re2c_unicode_file path expected_sha256)
  if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    message(FATAL_ERROR "re2c Unicode definitions are missing: ${path}")
  endif()
  file(READ "${path}" _unicode_text)
  string(REPLACE "\r\n" "\n" _unicode_text "${_unicode_text}")
  string(SHA256 _actual_sha256 "${_unicode_text}")
  if(NOT _actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR
      "Unsupported re2c Unicode data at ${path}; expected Unicode 17.0.0 from re2c 4.6 "
      "(LF SHA-256 ${expected_sha256}, got ${_actual_sha256})")
  endif()
endfunction()

function(salts_utils_verify_re2c_unicode stdlib_dir)
  salts_utils_verify_re2c_unicode_file(
    "${stdlib_dir}/unicode_categories.re"
    "3d0a319806e42a7b476f9d474c104f7e734c42eeab8fb36b2b82591cd55b0689")
  salts_utils_verify_re2c_unicode_file(
    "${stdlib_dir}/unicode_properties.re"
    "56b5f16e4e516a598125760ee7f4fae60a5db6727b8cf3dfb5f020152ef2aa21")
endfunction()

# The installer and FindTools.cmake consume exactly the same validation rules.
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(NOT DEFINED RE2C_STDLIB_DIR OR RE2C_STDLIB_DIR STREQUAL "")
    message(FATAL_ERROR "RE2C_STDLIB_DIR is required")
  endif()
  salts_utils_verify_re2c_unicode("${RE2C_STDLIB_DIR}")
endif()

cmake_policy(POP)

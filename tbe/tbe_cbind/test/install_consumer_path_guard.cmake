cmake_minimum_required(VERSION 3.20)

function(tbe_cbind_path_for_compare input_path output_variable)
  set(normalized_path "${input_path}")
  cmake_path(NORMAL_PATH normalized_path)
  if(WIN32)
    string(TOLOWER "${normalized_path}" normalized_path)
  endif()
  set(${output_variable} "${normalized_path}" PARENT_SCOPE)
endfunction()

function(tbe_cbind_refuse_unsafe_sandbox reason)
  message(FATAL_ERROR
    "Refusing unsafe install-consumer sandbox: ${reason}")
endfunction()

function(tbe_cbind_reset_install_consumer_sandbox
    main_directory candidate_directory validate_only)
  if("${main_directory}" STREQUAL "" OR
     "${candidate_directory}" STREQUAL "")
    tbe_cbind_refuse_unsafe_sandbox("empty path")
  endif()
  if(NOT IS_DIRECTORY "${main_directory}")
    tbe_cbind_refuse_unsafe_sandbox(
      "main build directory does not exist: ${main_directory}")
  endif()

  file(REAL_PATH "${main_directory}" main_real)
  set(main_normalized "${main_real}")
  cmake_path(NORMAL_PATH main_normalized)
  cmake_path(GET main_normalized ROOT_PATH filesystem_root)
  tbe_cbind_path_for_compare("${main_normalized}" main_compare)
  tbe_cbind_path_for_compare("${filesystem_root}" root_compare)
  if(main_compare STREQUAL root_compare)
    tbe_cbind_refuse_unsafe_sandbox("main build directory is a filesystem root")
  endif()

  set(candidate_normalized "${candidate_directory}")
  cmake_path(ABSOLUTE_PATH candidate_normalized
    BASE_DIRECTORY "${main_normalized}" NORMALIZE)
  set(expected_directory
    "${main_normalized}/tbe/tbe_cbind/install_consumer")
  cmake_path(NORMAL_PATH expected_directory)

  tbe_cbind_path_for_compare("${candidate_normalized}" candidate_compare)
  tbe_cbind_path_for_compare("${expected_directory}" expected_compare)
  cmake_path(IS_PREFIX main_compare "${candidate_compare}" NORMALIZE
    candidate_contained)
  if(NOT candidate_contained)
    tbe_cbind_refuse_unsafe_sandbox(
      "candidate is outside the main build directory: ${candidate_normalized}")
  endif()
  if(candidate_compare STREQUAL main_compare OR
     candidate_compare STREQUAL root_compare)
    tbe_cbind_refuse_unsafe_sandbox("candidate is a protected root")
  endif()
  if(NOT candidate_compare STREQUAL expected_compare)
    tbe_cbind_refuse_unsafe_sandbox(
      "candidate is not the fixed dedicated directory: ${candidate_normalized}")
  endif()

  foreach(relative_component IN ITEMS
      "tbe" "tbe/tbe_cbind" "tbe/tbe_cbind/install_consumer")
    set(component_path "${main_normalized}/${relative_component}")
    cmake_path(NORMAL_PATH component_path)
    if(IS_SYMLINK "${component_path}")
      tbe_cbind_refuse_unsafe_sandbox(
        "path component is a symbolic link: ${component_path}")
    endif()
    if(EXISTS "${component_path}")
      if(NOT IS_DIRECTORY "${component_path}")
        tbe_cbind_refuse_unsafe_sandbox(
          "path component is not a directory: ${component_path}")
      endif()
      file(REAL_PATH "${component_path}" component_real)
      tbe_cbind_path_for_compare("${component_path}" component_compare)
      tbe_cbind_path_for_compare("${component_real}" component_real_compare)
      if(NOT component_compare STREQUAL component_real_compare)
        tbe_cbind_refuse_unsafe_sandbox(
          "path component resolves through a link or junction: ${component_path}")
      endif()
      cmake_path(IS_PREFIX main_compare "${component_real_compare}" NORMALIZE
        component_contained)
      if(NOT component_contained)
        tbe_cbind_refuse_unsafe_sandbox(
          "resolved component escaped the main build directory: ${component_real}")
      endif()
    endif()
  endforeach()

  if(validate_only)
    return()
  endif()
  file(REMOVE_RECURSE "${candidate_normalized}")
  file(MAKE_DIRECTORY "${candidate_normalized}")
endfunction()

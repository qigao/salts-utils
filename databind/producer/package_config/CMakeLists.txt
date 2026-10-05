cmake_minimum_required(VERSION 3.27)
project(DataBindProducerInstalledConsumer LANGUAGES C)

enable_testing()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
  message(FATAL_ERROR "SALTS_UTILS_ROOT must name the installed SaltsUtils SDK")
endif()
unset(SaltsUtils_DIR CACHE)
find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

if(NOT TARGET Salts::DataBindProducer)
  message(FATAL_ERROR "missing installed Salts::DataBindProducer")
endif()

get_target_property(_producer_links
  Salts::DataBindProducer INTERFACE_LINK_LIBRARIES)
if(NOT "Salts::CMeta" IN_LIST _producer_links)
  message(FATAL_ERROR
    "installed Salts::DataBindProducer must expose canonical Salts::CMeta")
endif()
foreach(forbidden IN ITEMS Salts::DataBind Salts::DataBindCore)
  if("${forbidden}" IN_LIST _producer_links)
    message(FATAL_ERROR
      "installed Salts::DataBindProducer must not link ${forbidden}")
  endif()
endforeach()

add_executable(databind_producer_generate generate.c)
target_compile_features(databind_producer_generate PRIVATE c_std_11)
target_link_libraries(databind_producer_generate
  PRIVATE Salts::DataBindProducer)

set(GENERATED_PLAN
    "${CMAKE_CURRENT_BINARY_DIR}/generated_adapter_plan.h")

add_custom_command(
  OUTPUT "${GENERATED_PLAN}"
  COMMAND databind_producer_generate "${GENERATED_PLAN}"
  DEPENDS databind_producer_generate
  VERBATIM)

add_custom_target(databind_producer_generated
  DEPENDS "${GENERATED_PLAN}")

add_executable(databind_generated_plan_consumer
  consume.c "${GENERATED_PLAN}")
target_compile_features(databind_generated_plan_consumer PRIVATE c_std_11)
target_include_directories(databind_generated_plan_consumer
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
set_source_files_properties("${GENERATED_PLAN}"
  PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
add_dependencies(databind_generated_plan_consumer
  databind_producer_generated)
add_test(NAME databind_generated_plan_consumer
         COMMAND databind_generated_plan_consumer)

get_target_property(_consumer_links
  databind_generated_plan_consumer LINK_LIBRARIES)
if(_consumer_links)
  foreach(forbidden IN ITEMS
      Salts::DataBindProducer Salts::DataBind Salts::DataBindCore)
    if("${forbidden}" IN_LIST _consumer_links)
      message(FATAL_ERROR
        "generated adapter-plan consumer must not link ${forbidden}")
    endif()
  endforeach()
endif()

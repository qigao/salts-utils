set(DATA_BIND_PRODUCER_TARGET data_bind_producer)

# Producer/tooling-only projection from retained external CMeta manifests.
# Generated artifacts are self-contained C11 data and do not require this
# library (or any DataBind runtime target) in their consumer link closure.
add_library(${DATA_BIND_PRODUCER_TARGET} STATIC
  data_bind_cmeta_adapter_plan.c
  data_bind_cmeta_adapter_plan.h
  data_bind_typescript.c
  data_bind_typescript.h)

target_compile_features(${DATA_BIND_PRODUCER_TARGET} PUBLIC c_std_11)
target_include_directories(${DATA_BIND_PRODUCER_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(${DATA_BIND_PRODUCER_TARGET}
  PUBLIC Salts::Bindings)

cmake_config_target(
  ${DATA_BIND_PRODUCER_TARGET}
  ALIAS Salts::DataBindProducer
  FOLDER "databind/compiler"
  EXPORT_NAME DataBindProducer)

install(
  TARGETS ${DATA_BIND_PRODUCER_TARGET}
  EXPORT SaltsUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DataBind
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind)

install(
  FILES data_bind_cmeta_adapter_plan.h data_bind_typescript.h
  DESTINATION include
  COMPONENT DataBind)

if(BUILD_TESTS)
  add_executable(test_data_bind_cmeta_adapter_plan
    test_data_bind_cmeta_adapter_plan.c)
  target_compile_features(test_data_bind_cmeta_adapter_plan PRIVATE c_std_11)
  target_link_libraries(test_data_bind_cmeta_adapter_plan
    PRIVATE Salts::DataBindProducer)
  set_target_properties(test_data_bind_cmeta_adapter_plan PROPERTIES
    C_EXTENSIONS OFF)
  cmake_config_target(test_data_bind_cmeta_adapter_plan
    FOLDER "databind/compiler")
  add_test(NAME test_data_bind_cmeta_adapter_plan
    COMMAND test_data_bind_cmeta_adapter_plan)
  set_tests_properties(test_data_bind_cmeta_adapter_plan PROPERTIES
    LABELS "databind;producer;cmeta;adapter-plan")
endif()

add_library(plugin_cflow_publisher_fixture SHARED
  fixture_plugin_cflow_publisher.c)
target_compile_features(plugin_cflow_publisher_fixture PRIVATE c_std_11)
target_link_libraries(plugin_cflow_publisher_fixture
  PRIVATE Salts::PluginCFlowABI)
set_target_properties(plugin_cflow_publisher_fixture PROPERTIES
  PREFIX ""
  OUTPUT_NAME "plugin_cflow_publisher_fixture")

cmake_add_test(plugin_cflow_publisher_provider_test
  SOURCES plugin_cflow_publisher_provider_test.c
  LIBS Salts::PluginCFlow Salts::TinyTest
  DEFS
    PLUGIN_CFLOW_FIXTURE_PATH="$<TARGET_FILE:plugin_cflow_publisher_fixture>"
  FOLDER "plugin-cflow/tests")
add_dependencies(plugin_cflow_publisher_provider_test
  plugin_cflow_publisher_fixture)
set_tests_properties(plugin_cflow_publisher_provider_test PROPERTIES
  LABELS "plugin;cflow;publisher;provider;lifecycle")

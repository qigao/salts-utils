cmake_minimum_required(VERSION 3.27)
project(PluginCFlowInstalledConsumer LANGUAGES C)

find_package(SaltsUtils CONFIG REQUIRED)

foreach(target_name IN ITEMS
    Salts::PluginABI
    Salts::Plugin
    Salts::PluginCFlowABI
    Salts::PluginCFlow)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "missing installed target: ${target_name}")
  endif()
endforeach()

get_target_property(_plugin_links Salts::Plugin INTERFACE_LINK_LIBRARIES)
if(_plugin_links AND _plugin_links MATCHES "CFlow")
  message(FATAL_ERROR "Salts::Plugin must remain CFlow-free")
endif()

add_executable(plugin_cflow_installed_consumer main.c)
target_compile_features(plugin_cflow_installed_consumer PRIVATE c_std_11)
target_link_libraries(plugin_cflow_installed_consumer
  PRIVATE Salts::PluginCFlow)

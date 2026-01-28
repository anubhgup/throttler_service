# Resource Manager Module

add_library(resource_manager STATIC
  resource_manager.cc
)

target_include_directories(resource_manager PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(resource_manager PUBLIC
  client_registry
  Threads::Threads
)

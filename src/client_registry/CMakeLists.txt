# Client Registry Module

add_library(client_registry STATIC
  client_registry.cc
)

target_include_directories(client_registry PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/token_bucket  # For Clock interface
)

target_link_libraries(client_registry PUBLIC
  token_bucket
  Threads::Threads
)

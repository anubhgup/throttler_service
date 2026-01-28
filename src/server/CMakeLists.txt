# Throttling Server Module

add_library(throttling_server STATIC
  throttling_server.cc
)

target_include_directories(throttling_server PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(throttling_server PUBLIC
  throttling_proto
  client_registry
  resource_manager
  ${GRPC_LIBRARIES}
)

# Throttling Client Library Module

add_library(throttling_client STATIC
  throttling_client.cc
)

target_include_directories(throttling_client PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(throttling_client PUBLIC
  throttling_proto
  token_bucket
  ${GRPC_LIBRARIES}
  Threads::Threads
)

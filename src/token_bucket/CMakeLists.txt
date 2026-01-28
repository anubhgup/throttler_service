# Token Bucket Module

add_library(token_bucket STATIC
  token_bucket.cc
)

target_include_directories(token_bucket PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link with threading library
target_link_libraries(token_bucket PUBLIC
  Threads::Threads
)

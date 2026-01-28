cmake_minimum_required(VERSION 3.16)
project(throttling_service VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ============================================================================
# Dependencies
# ============================================================================

# Find Protobuf
find_package(Protobuf REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

# Find gRPC
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(GRPCPP REQUIRED grpc++_reflection)

# Find gRPC C++ plugin
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
if(NOT GRPC_CPP_PLUGIN)
  message(FATAL_ERROR "grpc_cpp_plugin not found")
endif()
message(STATUS "Using grpc_cpp_plugin: ${GRPC_CPP_PLUGIN}")

# Find Threads
find_package(Threads REQUIRED)

# ============================================================================
# Proto Generation
# ============================================================================

set(PROTO_FILES
  ${CMAKE_SOURCE_DIR}/proto/throttling_service.proto
)

set(PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Generate protobuf and gRPC sources
set(PROTO_SRCS ${PROTO_GEN_DIR}/throttling_service.pb.cc)
set(PROTO_HDRS ${PROTO_GEN_DIR}/throttling_service.pb.h)
set(GRPC_SRCS ${PROTO_GEN_DIR}/throttling_service.grpc.pb.cc)
set(GRPC_HDRS ${PROTO_GEN_DIR}/throttling_service.grpc.pb.h)

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    --cpp_out=${PROTO_GEN_DIR}
    --grpc_out=${PROTO_GEN_DIR}
    --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
    -I${CMAKE_SOURCE_DIR}/proto
    ${PROTO_FILES}
  DEPENDS ${PROTO_FILES}
  COMMENT "Generating protobuf and gRPC sources"
)

# Proto library
add_library(throttling_proto STATIC
  ${PROTO_SRCS}
  ${GRPC_SRCS}
)

target_include_directories(throttling_proto PUBLIC
  ${PROTO_GEN_DIR}
  ${Protobuf_INCLUDE_DIRS}
  ${GRPC_INCLUDE_DIRS}
)

target_link_libraries(throttling_proto PUBLIC
  ${Protobuf_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${GRPCPP_LIBRARIES}
)

# Suppress warnings in generated code
target_compile_options(throttling_proto PRIVATE -w)

# ============================================================================
# Source Directories (to be added as modules are implemented)
# ============================================================================

# Module 2: Token Bucket
add_subdirectory(src/token_bucket)

# Module 3: Client Registry
# add_subdirectory(src/client_registry)

# Module 4: Resource Manager
# add_subdirectory(src/resource_manager)

# Module 5: gRPC Server
# add_subdirectory(src/server)

# Module 6: Client Library
# add_subdirectory(src/client)

# ============================================================================
# Main Executable (to be added in Module 7)
# ============================================================================

# add_executable(throttling_server src/main.cc)
# target_link_libraries(throttling_server
#   throttling_server_lib
#   throttling_proto
# )

# ============================================================================
# Testing
# ============================================================================

enable_testing()

# Google Test (fetch if not found)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Test executables will be added as modules are implemented
# add_executable(token_bucket_test tests/token_bucket_test.cc)
# target_link_libraries(token_bucket_test GTest::gtest_main token_bucket)
# gtest_discover_tests(token_bucket_test)

// Throttling Service gRPC Server Implementation
//
// Story:
// This module implements the gRPC service handlers. It wires together
// ClientRegistry and ResourceManager to handle client requests. Handlers
// are thin - they validate input, delegate to dependencies, and build responses.
//
// Error Handling:
// - Uses gRPC error codes (INVALID_ARGUMENT, NOT_FOUND, etc.)
// - Empty client_id -> INVALID_ARGUMENT
// - Client not registered -> NOT_FOUND
// - Resource not configured -> NOT_FOUND
// - Invalid rate_limit -> INVALID_ARGUMENT
//
// Thread Safety:
// Stateless handlers delegate to thread-safe dependencies.

#pragma once

#include <grpcpp/grpcpp.h>

#include "throttling_service.grpc.pb.h"
#include "client_registry.h"
#include "resource_manager.h"

namespace throttling {

/// gRPC service implementation for the throttling service.
///
/// Stateless - all state is in ClientRegistry and ResourceManager.
/// Thread-safe - delegates to thread-safe dependencies.
///
/// Example:
///   ClientRegistry registry;
///   ResourceManager manager(&registry);
///   ThrottlingServiceImpl service(&registry, &manager);
///   // Use service with grpc::ServerBuilder
class ThrottlingServiceImpl final : public ThrottlingService::Service {
 public:
  /// Constructs the service implementation.
  ///
  /// @param client_registry Registry for client management (not owned).
  /// @param resource_manager Manager for resource allocation (not owned).
  /// Both must outlive this object.
  ThrottlingServiceImpl(ClientRegistry* client_registry,
                        ResourceManager* resource_manager);

  ~ThrottlingServiceImpl() = default;

  // Non-copyable, non-movable
  ThrottlingServiceImpl(const ThrottlingServiceImpl&) = delete;
  ThrottlingServiceImpl& operator=(const ThrottlingServiceImpl&) = delete;

  // =========================================================================
  // gRPC Service Methods
  // =========================================================================

  /// Registers a client with the service.
  /// @return INVALID_ARGUMENT if client_id is empty.
  grpc::Status RegisterClient(
      grpc::ServerContext* context,
      const RegisterClientRequest* request,
      RegisterClientResponse* response) override;

  /// Unregisters a client from the service.
  /// @return Always OK (idempotent).
  grpc::Status UnregisterClient(
      grpc::ServerContext* context,
      const UnregisterClientRequest* request,
      UnregisterClientResponse* response) override;

  /// Processes a heartbeat from a client.
  /// @return INVALID_ARGUMENT if client_id empty, NOT_FOUND if not registered.
  grpc::Status Heartbeat(
      grpc::ServerContext* context,
      const HeartbeatRequest* request,
      HeartbeatResponse* response) override;

  /// Gets the allocation for a client on a resource.
  /// @return INVALID_ARGUMENT if client_id is empty.
  grpc::Status GetAllocation(
      grpc::ServerContext* context,
      const GetAllocationRequest* request,
      GetAllocationResponse* response) override;

  /// Sets the rate limit for a resource.
  /// @return INVALID_ARGUMENT if rate_limit < 0.
  grpc::Status SetResourceLimit(
      grpc::ServerContext* context,
      const SetResourceLimitRequest* request,
      SetResourceLimitResponse* response) override;

  /// Gets the rate limit and client count for a resource.
  /// @return NOT_FOUND if resource not configured.
  grpc::Status GetResourceLimit(
      grpc::ServerContext* context,
      const GetResourceLimitRequest* request,
      GetResourceLimitResponse* response) override;

 private:
  ClientRegistry* client_registry_;
  ResourceManager* resource_manager_;
};

}  // namespace throttling

// Throttling Service gRPC Server - Implementation
//
// See throttling_server.h for the Story and design description.

#include "throttling_server.h"

#include <set>

namespace throttling {

ThrottlingServiceImpl::ThrottlingServiceImpl(ClientRegistry* client_registry,
                                             ResourceManager* resource_manager)
    : client_registry_(client_registry), resource_manager_(resource_manager) {}

grpc::Status ThrottlingServiceImpl::RegisterClient(
    grpc::ServerContext* /*context*/,
    const RegisterClientRequest* request,
    RegisterClientResponse* response) {
  const std::string& client_id = request->client_id();

  // Validate input
  if (client_id.empty()) {
    response->set_success(false);
    response->set_error_message("client_id cannot be empty");
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "client_id cannot be empty");
  }

  // Register the client
  bool success = client_registry_->RegisterClient(client_id);
  response->set_success(success);

  return grpc::Status::OK;
}

grpc::Status ThrottlingServiceImpl::UnregisterClient(
    grpc::ServerContext* /*context*/,
    const UnregisterClientRequest* request,
    UnregisterClientResponse* response) {
  const std::string& client_id = request->client_id();

  // Unregister is idempotent - always succeeds
  client_registry_->UnregisterClient(client_id);
  response->set_success(true);

  return grpc::Status::OK;
}

grpc::Status ThrottlingServiceImpl::Heartbeat(
    grpc::ServerContext* /*context*/,
    const HeartbeatRequest* request,
    HeartbeatResponse* response) {
  const std::string& client_id = request->client_id();

  // Validate input
  if (client_id.empty()) {
    response->set_success(false);
    response->set_error_message("client_id cannot be empty");
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "client_id cannot be empty");
  }

  // Convert repeated field to set
  std::set<int64_t> resource_ids(request->resource_ids().begin(),
                                 request->resource_ids().end());

  // Process heartbeat
  bool success = client_registry_->Heartbeat(client_id, resource_ids);
  if (!success) {
    response->set_success(false);
    response->set_error_message("client not registered");
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "client not registered");
  }

  // Get allocations for all resources the client is interested in
  auto allocations = resource_manager_->GetAllAllocations(client_id);

  // Populate response
  response->set_success(true);
  auto* alloc_map = response->mutable_allocations();
  for (const auto& [resource_id, allocation] : allocations) {
    (*alloc_map)[resource_id] = allocation;
  }

  return grpc::Status::OK;
}

grpc::Status ThrottlingServiceImpl::GetAllocation(
    grpc::ServerContext* /*context*/,
    const GetAllocationRequest* request,
    GetAllocationResponse* response) {
  const std::string& client_id = request->client_id();
  int64_t resource_id = request->resource_id();

  // Validate input
  if (client_id.empty()) {
    response->set_success(false);
    response->set_error_message("client_id cannot be empty");
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "client_id cannot be empty");
  }

  // Get allocation
  double allocation = resource_manager_->GetAllocation(client_id, resource_id);

  response->set_success(true);
  response->set_allocated_rate(allocation);

  return grpc::Status::OK;
}

grpc::Status ThrottlingServiceImpl::SetResourceLimit(
    grpc::ServerContext* /*context*/,
    const SetResourceLimitRequest* request,
    SetResourceLimitResponse* response) {
  int64_t resource_id = request->resource_id();
  double rate_limit = request->rate_limit();

  // SetResourceLimit validates rate_limit >= 0
  bool success = resource_manager_->SetResourceLimit(resource_id, rate_limit);
  if (!success) {
    response->set_success(false);
    response->set_error_message("rate_limit must be >= 0");
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "rate_limit must be >= 0");
  }

  response->set_success(true);
  return grpc::Status::OK;
}

grpc::Status ThrottlingServiceImpl::GetResourceLimit(
    grpc::ServerContext* /*context*/,
    const GetResourceLimitRequest* request,
    GetResourceLimitResponse* response) {
  int64_t resource_id = request->resource_id();

  // Get resource info
  auto info = resource_manager_->GetResourceInfo(resource_id);
  if (!info.has_value()) {
    response->set_success(false);
    response->set_error_message("resource not found");
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "resource not found");
  }

  response->set_success(true);
  response->set_rate_limit(info->rate_limit);
  response->set_active_client_count(static_cast<int32_t>(info->active_client_count));

  return grpc::Status::OK;
}

}  // namespace throttling
